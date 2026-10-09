#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "huffman.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * Constantes del formato y de la implementación
 * ========================================================================= */
#define HUF_MAGIC           "HUF1"
#define HUF_MAX_LEN         15                 /* Longitud máxima de un código */
#define HUF_TABLA_DEC       (1u << HUF_MAX_LEN)
#define HUF_HDR_FIJO        (4 + 4 + 8 + 4)    /* magic, tam_bloque, tam_orig, n_bloques */
#define HUF_HDR_TOTAL       (HUF_HDR_FIJO + 256)
#define HUF_BLOQUE_DEFECTO  ((size_t)256 * 1024)
#define HUF_BLOQUE_MIN      ((size_t)1024)
#define HUF_BLOQUE_MAX      ((size_t)64 * 1024 * 1024)
/* Peor caso de un bloque comprimido: 15 bits por símbolo + relleno. */
#define HUF_COMP_MAX(bs)    ((size_t)(bs) * 2 + 16)

/* ============================================================================
 * Utilidades: errno -> texto (thread-safe), E/S completa, CRC-32, progreso
 * ========================================================================= */
static const char *texto_errno(int e, char *buf, size_t sz) {
    /* Variante GNU de strerror_r (devuelve char*), segura entre hilos. */
    return strerror_r(e, buf, sz);
}

static int pread_todo(int fd, void *dst, size_t n, uint64_t off) {
    uint8_t *p = (uint8_t *)dst;
    while (n > 0) {
        ssize_t r = pread(fd, p, n, (off_t)off);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) { errno = EIO; return -2; }   /* EOF inesperado: el archivo cambió */
        p += r; n -= (size_t)r; off += (uint64_t)r;
    }
    return 0;
}

static int write_todo(int fd, const void *src, size_t n) {
    const uint8_t *p = (const uint8_t *)src;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w; n -= (size_t)w;
    }
    return 0;
}

static int pwrite_todo(int fd, const void *src, size_t n, uint64_t off) {
    const uint8_t *p = (const uint8_t *)src;
    while (n > 0) {
        ssize_t w = pwrite(fd, p, n, (off_t)off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w; n -= (size_t)w; off += (uint64_t)w;
    }
    return 0;
}

static uint32_t crc_tabla[256];
static pthread_once_t crc_once = PTHREAD_ONCE_INIT;

static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_tabla[i] = c;
    }
}

static uint32_t crc32_calc(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_tabla[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void huf_progreso_iniciar(HufProgreso *p) {
    if (!p) return;
    p->total = 0; p->hecho = 0; p->cancelar = 0; p->fase = HUF_FASE_INACTIVA;
    p->notificar = NULL; p->notificar_ctx = NULL;
}
void huf_progreso_cancelar(HufProgreso *p) {
    if (p) __atomic_store_n(&p->cancelar, 1, __ATOMIC_SEQ_CST);
}
int huf_progreso_cancelado(const HufProgreso *p) {
    return p ? __atomic_load_n(&p->cancelar, __ATOMIC_SEQ_CST) : 0;
}
int huf_progreso_porcentaje(const HufProgreso *p) {
    if (!p) return 0;
    uint64_t total = __atomic_load_n(&p->total, __ATOMIC_RELAXED);
    uint64_t hecho = __atomic_load_n(&p->hecho, __ATOMIC_RELAXED);
    if (total == 0) return __atomic_load_n(&p->fase, __ATOMIC_RELAXED) == HUF_FASE_INACTIVA ? 0 : 100;
    if (hecho >= total) return 100;
    return (int)((hecho * 100) / total);
}
static void prog_sumar(HufProgreso *p, uint64_t n) {
    uint64_t antes = __atomic_fetch_add(&p->hecho, n, __ATOMIC_RELAXED);
    uint64_t total = __atomic_load_n(&p->total, __ATOMIC_RELAXED);
    if (p->notificar && total) {
        uint64_t a = antes * 100 / total, b = (antes + n) * 100 / total;
        if (b > 99) b = 99;                               /* el 100 % lo anuncia quien termina */
        if (b / 10 > a / 10) p->notificar(p->notificar_ctx, (int)(b / 10) * 10);
    }
}
static void prog_fase(HufProgreso *p, int f)       { __atomic_store_n(&p->fase, f, __ATOMIC_RELAXED); }

/* ============================================================================
 * POOL DE HILOS TRABAJADORES (persistente durante toda la operación)
 *
 * Los N hilos se crean una sola vez y duermen en cv_start. pool_iniciar()
 * publica una nueva "generación" (fn + arg) y los despierta; pool_esperar()
 * duerme en cv_fin hasta que el último trabajador termine la generación.
 * Ningún hilo hace espera activa: todo es pthread_cond_wait.
 * ========================================================================= */
typedef void (*PoolFn)(void *arg, int wid);
struct Pool;
typedef struct { struct Pool *pool; int id; } PoolArg;

typedef struct Pool {
    pthread_t *th;
    PoolArg *args;
    int n, creados;
    pthread_mutex_t mu;
    pthread_cond_t cv_start, cv_fin;
    uint64_t gen;
    int activos, apagar;
    PoolFn fn;
    void *arg;
} Pool;

static void *pool_hilo(void *p) {
    PoolArg *pa = (PoolArg *)p;
    Pool *pool = pa->pool;
    uint64_t mi_gen = 0;
    pthread_mutex_lock(&pool->mu);
    for (;;) {
        while (!pool->apagar && pool->gen == mi_gen)
            pthread_cond_wait(&pool->cv_start, &pool->mu);
        if (pool->apagar) break;
        mi_gen = pool->gen;
        PoolFn fn = pool->fn;
        void *arg = pool->arg;
        pthread_mutex_unlock(&pool->mu);
        fn(arg, pa->id);
        pthread_mutex_lock(&pool->mu);
        if (--pool->activos == 0) pthread_cond_signal(&pool->cv_fin);
    }
    pthread_mutex_unlock(&pool->mu);
    return NULL;
}

static void pool_destruir(Pool *pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->mu);
    pool->apagar = 1;
    pthread_cond_broadcast(&pool->cv_start);
    pthread_mutex_unlock(&pool->mu);
    for (int i = 0; i < pool->creados; i++) pthread_join(pool->th[i], NULL);   /* join limpio */
    pthread_cond_destroy(&pool->cv_start);
    pthread_cond_destroy(&pool->cv_fin);
    pthread_mutex_destroy(&pool->mu);
    free(pool->th);
    free(pool->args);
    free(pool);
}

static Pool *pool_crear(int n) {
    Pool *pool = (Pool *)calloc(1, sizeof(Pool));
    if (!pool) return NULL;
    pool->n = n;
    pool->th = (pthread_t *)calloc((size_t)n, sizeof(pthread_t));
    pool->args = (PoolArg *)calloc((size_t)n, sizeof(PoolArg));
    if (!pool->th || !pool->args) { free(pool->th); free(pool->args); free(pool); return NULL; }
    pthread_mutex_init(&pool->mu, NULL);
    pthread_cond_init(&pool->cv_start, NULL);
    pthread_cond_init(&pool->cv_fin, NULL);

    /* Los trabajadores heredan la máscara de señales: las bloqueamos todas
       para que SIGINT/SIGTERM las atienda siempre el hilo principal. */
    sigset_t todas, previa;
    sigfillset(&todas);
    pthread_sigmask(SIG_BLOCK, &todas, &previa);
    for (int i = 0; i < n; i++) {
        pool->args[i].pool = pool;
        pool->args[i].id = i;
        if (pthread_create(&pool->th[i], NULL, pool_hilo, &pool->args[i]) != 0) break;
        pool->creados++;
    }
    pthread_sigmask(SIG_SETMASK, &previa, NULL);

    if (pool->creados != n) { pool_destruir(pool); return NULL; }
    return pool;
}

static void pool_iniciar(Pool *pool, PoolFn fn, void *arg) {
    pthread_mutex_lock(&pool->mu);
    pool->fn = fn;
    pool->arg = arg;
    pool->activos = pool->n;
    pool->gen++;
    pthread_cond_broadcast(&pool->cv_start);
    pthread_mutex_unlock(&pool->mu);
}

static void pool_esperar(Pool *pool) {
    pthread_mutex_lock(&pool->mu);
    while (pool->activos > 0) pthread_cond_wait(&pool->cv_fin, &pool->mu);
    pthread_mutex_unlock(&pool->mu);
}

/* ============================================================================
 * TRABAJO (Job): estado compartido de una compresión/descompresión
 *
 * PROTOCOLO DE SINCRONIZACIÓN (resumen; detalle en NOTAS_PARCIAL2.md):
 *   - 'mu' protege: next_job, next_write, freq[], slots[], abort, rc, msg.
 *   - cv_slot  : el escritor espera aquí a que el slot 'next_write' esté listo.
 *   - cv_space : los trabajadores esperan aquí cuando la ventana está llena
 *                (next_job >= next_write + W).
 *   - Sin deadlock: el bloque 'next_write' SIEMPRE cae dentro de la ventana,
 *     así que algún trabajador puede producirlo y el escritor avanza.
 *   - El slot (idx % W) está libre cuando idx < next_write + W porque el
 *     escritor lo vacía ANTES de incrementar next_write.
 *   - Datos por bloque (clen[], crc[]) los escribe un único trabajador antes
 *     de publicar el slot bajo 'mu'; el escritor los lee después de tomar 'mu'
 *     (happens-before por el mutex). El hilo coordinador los lee al final,
 *     tras pool_esperar().
 * ========================================================================= */
typedef struct { uint8_t *buf; size_t len; int listo; } Slot;

struct Job;
typedef int (*ProcFn)(struct Job *, uint64_t idx, uint8_t *scratch, uint8_t **out, size_t *outlen);

typedef struct Job {
    size_t bs;
    uint64_t size, nblocks;
    int nthreads;
    size_t W;
    unsigned retardo_ms;
    HufProgreso *prog;
    int fd_in, fd_out;

    uint8_t lens[256];
    uint32_t code[256];
    uint16_t *dec;
    uint32_t *clen, *crc;
    uint64_t *off;
    ProcFn proc;

    pthread_mutex_t mu;
    pthread_cond_t cv_slot, cv_space;
    uint64_t next_job, next_write;
    uint64_t freq[256];
    Slot *slots;
    int abort, rc;
    char msg[256];
} Job;

static size_t bloque_len(const Job *j, uint64_t idx) {
    uint64_t ini = idx * j->bs;
    uint64_t resto = j->size - ini;
    return resto < j->bs ? (size_t)resto : j->bs;
}

static void job_abortar(Job *j, int rc, const char *fmt, ...) {
    pthread_mutex_lock(&j->mu);
    if (!j->abort) {
        j->abort = 1;
        j->rc = rc;
        if (fmt) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(j->msg, sizeof(j->msg), fmt, ap);
            va_end(ap);
        }
    }
    pthread_cond_broadcast(&j->cv_slot);
    pthread_cond_broadcast(&j->cv_space);
    pthread_mutex_unlock(&j->mu);
}

/* Pausa artificial por bloque (solo demos/pruebas de cancelación). */
static void pausa_demo(const Job *j) {
    if (j->retardo_ms) {
        struct timespec t = { (time_t)(j->retardo_ms / 1000), (long)(j->retardo_ms % 1000) * 1000000L };
        nanosleep(&t, NULL);
    }
}

/* Retorna 1 si hay que parar (otro hilo abortó o el usuario canceló). */
static int job_detenido(Job *j) {
    if (huf_progreso_cancelado(j->prog)) {
        job_abortar(j, HUF_CANCELADO, "operación cancelada");
        return 1;
    }
    pthread_mutex_lock(&j->mu);
    int a = j->abort;
    pthread_mutex_unlock(&j->mu);
    return a;
}

static void job_err_io(Job *j, const char *que, int e) {
    char tmp[128];
    job_abortar(j, HUF_ERROR, "%s: %s", que, texto_errno(e, tmp, sizeof(tmp)));
}

/* ============================================================================
 * Árbol de Huffman -> longitudes -> códigos canónicos
 * ========================================================================= */

/* Calcula longitudes de código (<= HUF_MAX_LEN) a partir de las frecuencias.
   Si el árbol resulta demasiado profundo, se atenúan las frecuencias
   (f = (f+1)/2) y se reconstruye: produce un código válido, apenas subóptimo. */
static void huf_longitudes(const uint64_t *freq, uint8_t *lens) {
    uint64_t f[256];
    memcpy(f, freq, sizeof(f));
    for (;;) {
        memset(lens, 0, 256);
        int nsym = 0, ultimo = 0;
        for (int i = 0; i < 256; i++) if (f[i]) { nsym++; ultimo = i; }
        if (nsym == 0) return;
        if (nsym == 1) { lens[ultimo] = 1; return; }

        uint64_t peso[512];
        int padre[512], vivo[512];
        for (int i = 0; i < 512; i++) { peso[i] = 0; padre[i] = -1; vivo[i] = 0; }
        for (int i = 0; i < 256; i++) if (f[i]) { peso[i] = f[i]; vivo[i] = 1; }
        int sig = 256;
        for (int paso = 0; paso < nsym - 1; paso++) {
            int a = -1, b = -1;
            for (int i = 0; i < sig; i++) {          /* empates: menor id primero (determinista) */
                if (!vivo[i]) continue;
                if (a < 0 || peso[i] < peso[a]) { b = a; a = i; }
                else if (b < 0 || peso[i] < peso[b]) b = i;
            }
            peso[sig] = peso[a] + peso[b];
            vivo[a] = vivo[b] = 0; vivo[sig] = 1;
            padre[a] = padre[b] = sig;
            sig++;
        }
        int max = 0;
        for (int i = 0; i < 256; i++) {
            if (!f[i]) continue;
            int d = 0;
            for (int n = i; padre[n] != -1; n = padre[n]) d++;
            lens[i] = (uint8_t)(d > 255 ? 255 : d);
            if (d > max) max = d;
        }
        if (max <= HUF_MAX_LEN) return;
        for (int i = 0; i < 256; i++) if (f[i]) f[i] = (f[i] + 1) >> 1;
    }
}

/* Códigos canónicos (estilo DEFLATE). Retorna 0 si las longitudes son inválidas. */
static int huf_codigos(const uint8_t *lens, uint32_t *code) {
    uint32_t cuenta[HUF_MAX_LEN + 2] = {0}, sig[HUF_MAX_LEN + 2] = {0};
    uint64_t kraft = 0;
    for (int i = 0; i < 256; i++) {
        if (lens[i] > HUF_MAX_LEN) return 0;
        if (lens[i]) { cuenta[lens[i]]++; kraft += (uint64_t)1 << (HUF_MAX_LEN - lens[i]); }
    }
    if (kraft > ((uint64_t)1 << HUF_MAX_LEN)) return 0;      /* desigualdad de Kraft */
    uint32_t c = 0;
    for (int l = 1; l <= HUF_MAX_LEN; l++) { c = (c + cuenta[l - 1]) << 1; sig[l] = c; }
    for (int i = 0; i < 256; i++) code[i] = lens[i] ? sig[lens[i]]++ : 0;
    return 1;
}

static uint16_t *huf_tabla_decodificacion(const uint8_t *lens, const uint32_t *code) {
    uint16_t *t = (uint16_t *)calloc(HUF_TABLA_DEC, sizeof(uint16_t));
    if (!t) return NULL;
    for (int s = 0; s < 256; s++) {
        int l = lens[s];
        if (!l) continue;
        uint32_t ini = code[s] << (HUF_MAX_LEN - l);
        uint32_t n = 1u << (HUF_MAX_LEN - l);
        for (uint32_t k = 0; k < n; k++) t[ini + k] = (uint16_t)((l << 8) | s);
    }
    return t;
}

/* ============================================================================
 * Fase 1 (trabajador): conteo de frecuencias con reducción local
 * ========================================================================= */
static void trabajador_conteo(void *arg, int wid) {
    (void)wid;
    Job *j = (Job *)arg;
    uint64_t local[256];
    memset(local, 0, sizeof(local));
    uint8_t *buf = (uint8_t *)malloc(j->bs);
    if (!buf) { job_abortar(j, HUF_ERROR, "sin memoria (malloc)"); return; }

    for (;;) {
        if (job_detenido(j)) break;
        pthread_mutex_lock(&j->mu);
        if (j->next_job >= j->nblocks) { pthread_mutex_unlock(&j->mu); break; }
        uint64_t idx = j->next_job++;
        pthread_mutex_unlock(&j->mu);

        size_t len = bloque_len(j, idx);
        int r = pread_todo(j->fd_in, buf, len, idx * j->bs);
        if (r != 0) { job_err_io(j, "lectura del archivo", errno); break; }
        for (size_t i = 0; i < len; i++) local[buf[i]]++;     /* sin locks: histograma local */
        pausa_demo(j);
        prog_sumar(j->prog, len);
    }
    pthread_mutex_lock(&j->mu);                               /* reducción: una sola vez por hilo */
    for (int i = 0; i < 256; i++) j->freq[i] += local[i];
    pthread_mutex_unlock(&j->mu);
    free(buf);
}

/* ============================================================================
 * Fase 2 (trabajadores): procesar bloques; (coordinador): escribir en orden
 * ========================================================================= */
static void trabajador_tuberia(void *arg, int wid) {
    (void)wid;
    Job *j = (Job *)arg;
    uint8_t *scratch = (uint8_t *)malloc(HUF_COMP_MAX(j->bs) + 64);
    if (!scratch) { job_abortar(j, HUF_ERROR, "sin memoria (malloc)"); return; }

    for (;;) {
        if (job_detenido(j)) break;
        pthread_mutex_lock(&j->mu);
        /* Ventana acotada: no adelantarse más de W bloques al escritor. */
        while (!j->abort && j->next_job < j->nblocks && j->next_job >= j->next_write + j->W)
            pthread_cond_wait(&j->cv_space, &j->mu);
        if (j->abort || j->next_job >= j->nblocks) { pthread_mutex_unlock(&j->mu); break; }
        uint64_t idx = j->next_job++;
        pthread_mutex_unlock(&j->mu);

        uint8_t *out = NULL;
        size_t outlen = 0;
        if (j->proc(j, idx, scratch, &out, &outlen) != 0) { free(out); break; }  /* proc ya abortó */

        pthread_mutex_lock(&j->mu);
        Slot *s = &j->slots[idx % j->W];
        s->buf = out; s->len = outlen; s->listo = 1;
        if (idx == j->next_write) pthread_cond_signal(&j->cv_slot);
        pthread_mutex_unlock(&j->mu);
    }
    free(scratch);
}

typedef int (*SinkFn)(Job *, uint64_t idx, const uint8_t *buf, size_t len);

/* Coordinador: consume los slots estrictamente en orden 0,1,2,... */
static void escritor_ordenado(Job *j, SinkFn sink) {
    for (uint64_t i = 0; i < j->nblocks; i++) {
        if (huf_progreso_cancelado(j->prog)) { job_abortar(j, HUF_CANCELADO, "operación cancelada"); break; }
        pthread_mutex_lock(&j->mu);
        Slot *s = &j->slots[i % j->W];
        while (!j->abort && !s->listo) pthread_cond_wait(&j->cv_slot, &j->mu);
        if (j->abort) { pthread_mutex_unlock(&j->mu); break; }
        uint8_t *buf = s->buf;
        size_t len = s->len;
        pthread_mutex_unlock(&j->mu);

        int rc = sink(j, i, buf, len);                 /* E/S fuera del mutex */
        free(buf);

        pthread_mutex_lock(&j->mu);
        s->buf = NULL; s->listo = 0;                   /* liberar slot ANTES de avanzar */
        j->next_write = i + 1;
        pthread_cond_broadcast(&j->cv_space);
        pthread_mutex_unlock(&j->mu);
        if (rc != 0) break;                            /* sink ya abortó */
    }
}

static int ejecutar_tuberia(Job *j, Pool *pool, SinkFn sink) {
    j->next_job = 0;
    j->next_write = 0;
    pool_iniciar(pool, trabajador_tuberia, j);
    escritor_ordenado(j, sink);
    /* Si el escritor terminó por abort/cancelación los trabajadores ya fueron
       despertados por job_abortar(); si terminó bien, ya no quedan bloques. */
    pool_esperar(pool);
    for (size_t k = 0; k < j->W; k++) { free(j->slots[k].buf); j->slots[k].buf = NULL; }
    return j->abort ? j->rc : HUF_OK;
}

/* ============================================================================
 * Compresión: proc (codificar un bloque) y sink (escribir a disco)
 * ========================================================================= */
static int proc_codificar(Job *j, uint64_t idx, uint8_t *scratch, uint8_t **out, size_t *outlen) {
    size_t len = bloque_len(j, idx);
    if (pread_todo(j->fd_in, scratch, len, idx * j->bs) != 0) { job_err_io(j, "lectura del archivo", errno); return -1; }
    j->crc[idx] = crc32_calc(scratch, len);

    uint8_t *dst = (uint8_t *)malloc(HUF_COMP_MAX(len));
    if (!dst) { job_abortar(j, HUF_ERROR, "sin memoria (malloc)"); return -1; }
    size_t o = 0;
    uint64_t acc = 0;
    int nb = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t s = scratch[i];
        if (j->lens[s] == 0) {   /* símbolo que el conteo no vio: la entrada cambió entre fases */
            free(dst);
            job_abortar(j, HUF_ERROR, "el archivo de entrada cambió durante la compresión");
            return -1;
        }
        acc = (acc << j->lens[s]) | j->code[s];
        nb += j->lens[s];
        while (nb >= 8) { dst[o++] = (uint8_t)(acc >> (nb - 8)); nb -= 8; }
        acc &= ((uint64_t)1 << nb) - 1;
    }
    if (nb > 0) dst[o++] = (uint8_t)(acc << (8 - nb));
    j->clen[idx] = (uint32_t)o;
    *out = dst; *outlen = o;
    pausa_demo(j);
    prog_sumar(j->prog, len);
    return 0;
}

static int sink_escribir(Job *j, uint64_t idx, const uint8_t *buf, size_t len) {
    (void)idx;
    if (write_todo(j->fd_out, buf, len) != 0) { job_err_io(j, "escritura de la salida", errno); return -1; }
    return 0;
}

/* ============================================================================
 * Descompresión: proc (leer + decodificar + verificar CRC) y sink
 * ========================================================================= */
static int proc_decodificar(Job *j, uint64_t idx, uint8_t *scratch, uint8_t **out, size_t *outlen) {
    size_t clen = j->clen[idx];
    size_t olen = bloque_len(j, idx);
    if (clen > 0 && pread_todo(j->fd_in, scratch, clen, j->off[idx]) != 0) {
        job_err_io(j, "lectura del archivo comprimido", errno); return -1;
    }
    uint8_t *dst = (uint8_t *)malloc(olen ? olen : 1);
    if (!dst) { job_abortar(j, HUF_ERROR, "sin memoria (malloc)"); return -1; }

    uint64_t acc = 0;
    int nb = 0;
    size_t pos = 0;
    for (size_t i = 0; i < olen; i++) {
        while (nb <= 56 && pos < clen) { acc = (acc << 8) | scratch[pos++]; nb += 8; }
        uint32_t ventana = nb >= HUF_MAX_LEN ? (uint32_t)(acc >> (nb - HUF_MAX_LEN))
                                             : (uint32_t)(acc << (HUF_MAX_LEN - nb));
        uint16_t e = j->dec[ventana & (HUF_TABLA_DEC - 1)];
        int l = e >> 8;
        if (l == 0 || l > nb) {
            free(dst);
            job_abortar(j, HUF_ERROR, "datos corruptos en el bloque %llu", (unsigned long long)idx);
            return -1;
        }
        dst[i] = (uint8_t)(e & 0xFF);
        nb -= l;
        acc &= nb ? (((uint64_t)1 << nb) - 1) : 0;
    }
    if (crc32_calc(dst, olen) != j->crc[idx]) {
        free(dst);
        job_abortar(j, HUF_ERROR, "CRC-32 incorrecto en el bloque %llu (archivo dañado)", (unsigned long long)idx);
        return -1;
    }
    *out = dst; *outlen = olen;
    pausa_demo(j);
    prog_sumar(j->prog, olen);
    return 0;
}

/* ============================================================================
 * Infraestructura común: crear/limpiar Job, archivo temporal + rename()
 * ========================================================================= */
static int job_crear(Job *j, size_t bs, uint64_t size, const HufOpciones *op, HufProgreso *prog) {
    memset(j, 0, sizeof(*j));
    j->bs = bs;
    j->size = size;
    j->nblocks = size ? (size + bs - 1) / bs : 0;
    j->prog = prog;
    j->fd_in = j->fd_out = -1;

    int hilos = op ? op->hilos : 0;
    if (hilos <= 0) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        hilos = cpus > 0 ? (int)cpus : 2;
    }
    if (hilos > 64) hilos = 64;
    if ((uint64_t)hilos > j->nblocks) hilos = (int)(j->nblocks ? j->nblocks : 1);
    j->nthreads = hilos;
    j->retardo_ms = op ? op->retardo_ms : 0;
    j->W = (op && op->ventana) ? op->ventana : (size_t)hilos * 4;
    if (j->W < (size_t)hilos) j->W = (size_t)hilos;

    pthread_mutex_init(&j->mu, NULL);
    pthread_cond_init(&j->cv_slot, NULL);
    pthread_cond_init(&j->cv_space, NULL);
    j->slots = (Slot *)calloc(j->W, sizeof(Slot));
    j->clen = (uint32_t *)calloc(j->nblocks ? j->nblocks : 1, sizeof(uint32_t));
    j->crc = (uint32_t *)calloc(j->nblocks ? j->nblocks : 1, sizeof(uint32_t));
    if (!j->slots || !j->clen || !j->crc) return -1;
    return 0;
}

static void job_liberar(Job *j) {
    free(j->slots); free(j->clen); free(j->crc); free(j->off); free(j->dec);
    pthread_cond_destroy(&j->cv_slot);
    pthread_cond_destroy(&j->cv_space);
    pthread_mutex_destroy(&j->mu);
}

static void poner_err(char *err, size_t sz, const char *fmt, ...) {
    if (!err || sz == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, sz, fmt, ap);
    va_end(ap);
}

/* Crea "<ruta>.XXXXXX" (mkstemp, O_EXCL) en el mismo directorio de destino. */
static int abrir_temporal(const char *ruta, char **tmp_out) {
    size_t n = strlen(ruta) + 8;
    char *tmp = (char *)malloc(n);
    if (!tmp) { errno = ENOMEM; return -1; }
    snprintf(tmp, n, "%s.XXXXXX", ruta);
    int fd = mkstemp(tmp);
    if (fd == -1) { int e = errno; free(tmp); errno = e; return -1; }
    *tmp_out = tmp;
    return fd;
}

/* Cierra el temporal; si 'publicar' lo renombra a su destino, si no lo borra. */
static int cerrar_temporal(int *fd, char *tmp, const char *destino, int publicar, char *err, size_t errsz) {
    int ok = 1;
    if (publicar) {
        if (fchmod(*fd, 0644) == -1 || fsync(*fd) == -1) {
            char b[128]; poner_err(err, errsz, "fsync/fchmod: %s", texto_errno(errno, b, sizeof(b))); ok = 0;
        }
    }
    if (close(*fd) == -1 && ok && publicar) {
        char b[128]; poner_err(err, errsz, "close: %s", texto_errno(errno, b, sizeof(b))); ok = 0;
    }
    *fd = -1;
    if (publicar && ok) {
        if (rename(tmp, destino) == -1) {
            char b[128]; poner_err(err, errsz, "rename: %s", texto_errno(errno, b, sizeof(b))); ok = 0;
        }
    }
    if (!(publicar && ok)) unlink(tmp);                 /* nunca dejar temporales huérfanos */
    free(tmp);
    return ok ? 0 : -1;
}

/* Serialización little-endian explícita (independiente de la arquitectura). */
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) { uint32_t v = 0; for (int i = 3; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static uint64_t get64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }

/* ============================================================================
 * API: comprimir
 * ========================================================================= */
int huf_comprimir_fd(int fd_in, const char *ruta_out, const HufOpciones *op,
                     HufProgreso *prog, uint64_t *bytes_out, char *err, size_t err_sz) {
    HufProgreso local_prog;
    if (!prog) { huf_progreso_iniciar(&local_prog); prog = &local_prog; }
    pthread_once(&crc_once, crc_init);

    struct stat st;
    if (fstat(fd_in, &st) == -1) {
        char b[128]; poner_err(err, err_sz, "fstat: %s", texto_errno(errno, b, sizeof(b))); return HUF_ERROR;
    }
    if (!S_ISREG(st.st_mode)) { poner_err(err, err_sz, "la entrada no es un archivo regular"); return HUF_ERROR; }

    size_t bs = (op && op->tam_bloque) ? op->tam_bloque : HUF_BLOQUE_DEFECTO;
    if (bs < HUF_BLOQUE_MIN || bs > HUF_BLOQUE_MAX) { poner_err(err, err_sz, "tamaño de bloque fuera de rango"); return HUF_ERROR; }
    uint64_t size = (uint64_t)st.st_size;
    if (size && (size + bs - 1) / bs > 0xFFFFFFFFull) { poner_err(err, err_sz, "archivo demasiado grande para este bloque"); return HUF_ERROR; }

    Job j;
    Pool *pool = NULL;
    char *tmp = NULL;
    int fd_out = -1, rc = HUF_ERROR;
    uint8_t *hdr = NULL;

    if (job_crear(&j, bs, size, op, prog) != 0) { poner_err(err, err_sz, "sin memoria (malloc)"); job_liberar(&j); return HUF_ERROR; }
    j.fd_in = fd_in;
    j.proc = proc_codificar;
    __atomic_store_n(&prog->hecho, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&prog->total, size * 2, __ATOMIC_RELAXED);   /* conteo + codificación */
    prog_fase(prog, HUF_FASE_CONTEO);

    if (j.nblocks > 0) {
        pool = pool_crear(j.nthreads);
        if (!pool) { poner_err(err, err_sz, "no se pudieron crear los hilos trabajadores"); goto fin; }
        /* ---- Fase 1: conteo de frecuencias en paralelo ---- */
        j.next_job = 0;
        pool_iniciar(pool, trabajador_conteo, &j);
        pool_esperar(pool);
        if (j.abort) { rc = j.rc; poner_err(err, err_sz, "%s", j.msg); goto fin; }
    }

    /* ---- Árbol global + códigos canónicos (hilo coordinador) ---- */
    huf_longitudes(j.freq, j.lens);
    if (!huf_codigos(j.lens, j.code)) { poner_err(err, err_sz, "error interno construyendo códigos"); goto fin; }

    fd_out = abrir_temporal(ruta_out, &tmp);
    if (fd_out == -1) {
        char b[128]; poner_err(err, err_sz, "no se pudo crear la salida '%s': %s", ruta_out, texto_errno(errno, b, sizeof(b)));
        goto fin;
    }
    j.fd_out = fd_out;
    size_t hdr_sz = HUF_HDR_TOTAL + (size_t)j.nblocks * 8;
    if (lseek(fd_out, (off_t)hdr_sz, SEEK_SET) == -1) {
        char b[128]; poner_err(err, err_sz, "lseek: %s", texto_errno(errno, b, sizeof(b))); goto fin;
    }

    /* ---- Fase 2: codificación paralela + escritura ordenada ---- */
    prog_fase(prog, HUF_FASE_CODIFICA);
    rc = HUF_OK;
    if (j.nblocks > 0) rc = ejecutar_tuberia(&j, pool, sink_escribir);
    if (rc != HUF_OK) { poner_err(err, err_sz, "%s", j.msg); goto fin; }

    /* ---- Cabecera + tabla de bloques (ahora que se conocen los tamaños) ---- */
    hdr = (uint8_t *)calloc(1, hdr_sz);
    if (!hdr) { poner_err(err, err_sz, "sin memoria (malloc)"); rc = HUF_ERROR; goto fin; }
    memcpy(hdr, HUF_MAGIC, 4);
    put32(hdr + 4, (uint32_t)bs);
    put64(hdr + 8, size);
    put32(hdr + 16, (uint32_t)j.nblocks);
    memcpy(hdr + HUF_HDR_FIJO, j.lens, 256);
    uint64_t total_out = hdr_sz;
    for (uint64_t i = 0; i < j.nblocks; i++) {
        put32(hdr + HUF_HDR_TOTAL + i * 8, j.clen[i]);
        put32(hdr + HUF_HDR_TOTAL + i * 8 + 4, j.crc[i]);
        total_out += j.clen[i];
    }
    if (pwrite_todo(fd_out, hdr, hdr_sz, 0) != 0) {
        char b[128]; poner_err(err, err_sz, "escritura de cabecera: %s", texto_errno(errno, b, sizeof(b))); rc = HUF_ERROR; goto fin;
    }
    if (bytes_out) *bytes_out = total_out;

fin:
    free(hdr);
    if (pool) pool_destruir(pool);                       /* join de todos los hilos */
    if (fd_out != -1) {
        char e2[160] = "";
        if (cerrar_temporal(&fd_out, tmp, ruta_out, rc == HUF_OK, e2, sizeof(e2)) != 0 && rc == HUF_OK) {
            rc = HUF_ERROR; poner_err(err, err_sz, "%s", e2);
        }
    }
    job_liberar(&j);
    prog_fase(prog, HUF_FASE_INACTIVA);
    if (rc == HUF_CANCELADO) poner_err(err, err_sz, "operación cancelada");
    return rc;
}

int huf_comprimir_archivo(const char *ruta_in, const char *ruta_out, const HufOpciones *op,
                          HufProgreso *prog, uint64_t *bytes_out, char *err, size_t err_sz) {
    int fd = open(ruta_in, O_RDONLY);
    if (fd == -1) {
        char b[128]; poner_err(err, err_sz, "open('%s'): %s", ruta_in, texto_errno(errno, b, sizeof(b))); return HUF_ERROR;
    }
    int rc = huf_comprimir_fd(fd, ruta_out, op, prog, bytes_out, err, err_sz);
    close(fd);
    return rc;
}

/* ============================================================================
 * API: descomprimir
 * ========================================================================= */
int huf_descomprimir_archivo(const char *ruta_in, const char *ruta_out, const HufOpciones *op,
                             HufProgreso *prog, uint64_t *bytes_out, char *err, size_t err_sz) {
    HufProgreso local_prog;
    if (!prog) { huf_progreso_iniciar(&local_prog); prog = &local_prog; }
    pthread_once(&crc_once, crc_init);

    int fd_in = open(ruta_in, O_RDONLY);
    if (fd_in == -1) {
        char b[128]; poner_err(err, err_sz, "open('%s'): %s", ruta_in, texto_errno(errno, b, sizeof(b))); return HUF_ERROR;
    }
    struct stat st;
    uint8_t h[HUF_HDR_TOTAL];
    uint8_t *tabla = NULL;
    Job j;
    int job_ok = 0, rc = HUF_ERROR, fd_out = -1;
    char *tmp = NULL;
    Pool *pool = NULL;

    if (fstat(fd_in, &st) == -1 || !S_ISREG(st.st_mode)) { poner_err(err, err_sz, "'%s' no es un archivo regular", ruta_in); goto fin; }
    uint64_t fsize = (uint64_t)st.st_size;
    if (fsize < HUF_HDR_TOTAL || pread_todo(fd_in, h, HUF_HDR_TOTAL, 0) != 0 || memcmp(h, HUF_MAGIC, 4) != 0) {
        poner_err(err, err_sz, "no es un archivo .huf válido (firma incorrecta)"); goto fin;
    }
    uint64_t bs = get32(h + 4), size = get64(h + 8), nb = get32(h + 16);
    if (bs < HUF_BLOQUE_MIN || bs > HUF_BLOQUE_MAX || nb != (size ? (size + bs - 1) / bs : 0)) {
        poner_err(err, err_sz, "cabecera inconsistente"); goto fin;
    }
    uint64_t hdr_sz = HUF_HDR_TOTAL + nb * 8;
    if (hdr_sz > fsize) { poner_err(err, err_sz, "archivo truncado (tabla de bloques incompleta)"); goto fin; }

    if (job_crear(&j, (size_t)bs, size, op, prog) != 0) { job_ok = 1; poner_err(err, err_sz, "sin memoria (malloc)"); goto fin; }
    job_ok = 1;
    j.fd_in = fd_in;
    j.proc = proc_decodificar;
    j.off = (uint64_t *)calloc(nb ? nb : 1, sizeof(uint64_t));
    tabla = (uint8_t *)malloc(nb ? nb * 8 : 1);
    if (!j.off || !tabla) { poner_err(err, err_sz, "sin memoria (malloc)"); goto fin; }
    if (nb && pread_todo(fd_in, tabla, (size_t)(nb * 8), HUF_HDR_TOTAL) != 0) {
        poner_err(err, err_sz, "archivo truncado (tabla de bloques)"); goto fin;
    }
    uint64_t pos = hdr_sz;
    for (uint64_t i = 0; i < nb; i++) {
        j.clen[i] = get32(tabla + i * 8);
        j.crc[i] = get32(tabla + i * 8 + 4);
        if (j.clen[i] > HUF_COMP_MAX(bs)) { poner_err(err, err_sz, "tamaño de bloque comprimido inválido"); goto fin; }
        j.off[i] = pos;
        pos += j.clen[i];
    }
    if (pos != fsize) { poner_err(err, err_sz, "archivo truncado o con datos sobrantes"); goto fin; }

    memcpy(j.lens, h + HUF_HDR_FIJO, 256);
    if (!huf_codigos(j.lens, j.code)) { poner_err(err, err_sz, "tabla de códigos inválida"); goto fin; }
    j.dec = huf_tabla_decodificacion(j.lens, j.code);
    if (!j.dec) { poner_err(err, err_sz, "sin memoria (malloc)"); goto fin; }

    __atomic_store_n(&prog->hecho, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&prog->total, size, __ATOMIC_RELAXED);
    prog_fase(prog, HUF_FASE_DECODIFICA);

    fd_out = abrir_temporal(ruta_out, &tmp);
    if (fd_out == -1) {
        char b[128]; poner_err(err, err_sz, "no se pudo crear la salida '%s': %s", ruta_out, texto_errno(errno, b, sizeof(b)));
        goto fin;
    }
    j.fd_out = fd_out;
    rc = HUF_OK;
    if (nb > 0) {
        pool = pool_crear(j.nthreads);
        if (!pool) { poner_err(err, err_sz, "no se pudieron crear los hilos trabajadores"); rc = HUF_ERROR; goto fin; }
        rc = ejecutar_tuberia(&j, pool, sink_escribir);
        if (rc != HUF_OK) poner_err(err, err_sz, "%s", j.msg);
    }
    if (rc == HUF_OK && bytes_out) *bytes_out = size;

fin:
    if (pool) pool_destruir(pool);
    if (fd_out != -1) {
        char e2[160] = "";
        if (cerrar_temporal(&fd_out, tmp, ruta_out, rc == HUF_OK, e2, sizeof(e2)) != 0 && rc == HUF_OK) {
            rc = HUF_ERROR; poner_err(err, err_sz, "%s", e2);
        }
    }
    if (job_ok) job_liberar(&j);
    free(tabla);
    close(fd_in);
    prog_fase(prog, HUF_FASE_INACTIVA);
    if (rc == HUF_CANCELADO) poner_err(err, err_sz, "operación cancelada");
    return rc;
}
