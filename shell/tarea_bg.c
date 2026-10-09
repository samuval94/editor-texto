#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "tarea_bg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Salida de mensajes del hilo de fondo: un único write() por mensaje (atómico
 * para líneas cortas) para no mezclar bytes con la salida del hilo UI.
 * Si stdout es una terminal, borra la línea actual y vuelve a pintar el prompt
 * para que el aviso no estropee la línea de entrada.
 * ------------------------------------------------------------------------- */
static void aviso(TareaBg *tb, const char *fmt, ...) {
    char buf[600];
    size_t n = 0;
    if (tb->es_tty) n += (size_t)snprintf(buf + n, sizeof(buf) - n, "\r\033[K");
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (r < 0) return;
    n += (size_t)r;
    if (n > sizeof(buf) - 16) n = sizeof(buf) - 16;
    buf[n++] = '\n';
    if (tb->es_tty) { memcpy(buf + n, PROMPT_EDITOR, strlen(PROMPT_EDITOR)); n += strlen(PROMPT_EDITOR); }
    ssize_t w = write(STDOUT_FILENO, buf, n);
    (void)w;
}

static char *copiar(const char *s) {
    size_t n = strlen(s) + 1;
    char *c = (char *)malloc(n);
    if (c) memcpy(c, s, n);
    return c;
}

/* Ruta canónica aunque el archivo aún no exista: realpath() del directorio + nombre base.
   Evita que alias como "./x.huf", "d/../x.huf" o un symlink burlen la reserva de rutas
   cuando el destino todavía no está creado (se escribe a un temporal y se publica con rename()). */
static char *ruta_canonica(const char *ruta) {
    char *res = realpath(ruta, NULL);
    if (res) return res;
    if (errno != ENOENT) return NULL;
    const char *barra = strrchr(ruta, '/');
    const char *base = barra ? barra + 1 : ruta;
    char *dir;
    if (!barra) {
        dir = realpath(".", NULL);
    } else if (barra == ruta) {
        dir = realpath("/", NULL);
    } else {
        size_t n = (size_t)(barra - ruta);
        char *d = (char *)malloc(n + 1);
        if (!d) return NULL;
        memcpy(d, ruta, n);
        d[n] = '\0';
        dir = realpath(d, NULL);
        free(d);
    }
    if (!dir) return NULL;
    size_t ld = strlen(dir), lb = strlen(base);
    char *out = (char *)malloc(ld + lb + 2);
    if (out) {
        memcpy(out, dir, ld);
        size_t pos = ld;
        if (pos == 0 || out[pos - 1] != '/') out[pos++] = '/';
        memcpy(out + pos, base, lb + 1);
    }
    free(dir);
    return out;
}

int tarea_bg_misma_ruta(const char *a, const char *b) {
    if (!a || !b) return 0;
    if (strcmp(a, b) == 0) return 1;
    int igual = 0;
    char *ca = ruta_canonica(a), *cb = ruta_canonica(b);
    if (ca && cb && strcmp(ca, cb) == 0) igual = 1;
    free(ca); free(cb);
    if (igual) return 1;
    struct stat sa, sb;
    if (stat(a, &sa) == 0 && stat(b, &sb) == 0)
        return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
    return 0;
}

void tarea_bg_inicializar(TareaBg *tb) {
    memset(tb, 0, sizeof(*tb));
    pthread_mutex_init(&tb->mu, NULL);
    pthread_cond_init(&tb->cv_fin, NULL);
    tb->fd_snapshot = -1;
    tb->es_tty = isatty(STDOUT_FILENO);
    huf_progreso_iniciar(&tb->prog);
}

/* Callback de progreso (lo llaman los hilos trabajadores al cruzar 10 %, 20 %...). */
static void al_progresar(void *ctx, int pct) {
    TareaBg *tb = (TareaBg *)ctx;
    pthread_mutex_lock(&tb->mu);
    if (tb->estado == TB_CORRIENDO && pct > tb->ultimo_hito) {
        tb->ultimo_hito = pct;
        aviso(tb, "[bg] %s: %d%%", tb->tipo == TB_COMPRIMIR ? "Comprimiendo" : "Descomprimiendo", pct);
    }
    pthread_mutex_unlock(&tb->mu);
}

static double segundos_desde(const struct timespec *t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (double)(t1.tv_sec - t0->tv_sec) + (double)(t1.tv_nsec - t0->tv_nsec) / 1e9;
}

/* Cuerpo del hilo coordinador de la tarea. */
static void *hilo_tarea(void *arg) {
    TareaBg *tb = (TareaBg *)arg;
    HufOpciones op = {0, 0, 0, 0};
    const char *lento = getenv("EAFIT_HUF_RETARDO_MS");   /* solo para demos/pruebas */
    if (lento) op.retardo_ms = (unsigned)strtoul(lento, NULL, 10);
    uint64_t salida = 0;
    char err[256] = "";
    int rc;

    if (tb->tipo == TB_COMPRIMIR) {
        rc = huf_comprimir_fd(tb->fd_snapshot, tb->destino, &op, &tb->prog, &salida, err, sizeof(err));
        close(tb->fd_snapshot);                         /* el snapshot ya era anónimo (unlink) */
    } else {
        rc = huf_descomprimir_archivo(tb->origen, tb->destino, &op, &tb->prog, &salida, err, sizeof(err));
    }

    pthread_mutex_lock(&tb->mu);
    tb->fd_snapshot = -1;
    tb->resultado = rc;
    tb->bytes_out = salida;
    snprintf(tb->err, sizeof(tb->err), "%s", err);
    double seg = segundos_desde(&tb->inicio);
    if (rc == HUF_OK && tb->tipo == TB_COMPRIMIR) {
        double ratio = tb->bytes_in ? 100.0 * (double)salida / (double)tb->bytes_in : 0.0;
        aviso(tb, "[bg] Compresión terminada: '%s' -> '%s' (%llu -> %llu bytes, %.1f%% del original, %.2f s)",
              tb->origen, tb->destino, (unsigned long long)tb->bytes_in, (unsigned long long)salida, ratio, seg);
        if (tb->ediciones > 0)
            aviso(tb, "[bg] Aviso: el documento se editó %d vez/veces durante la compresión; "
                      "el .huf contiene la versión del momento de iniciar (snapshot).", tb->ediciones);
    } else if (rc == HUF_OK) {
        aviso(tb, "[bg] Descompresión terminada: '%s' -> '%s' (%llu bytes, CRC-32 verificado, %.2f s)",
              tb->origen, tb->destino, (unsigned long long)salida, seg);
    } else if (rc == HUF_CANCELADO) {
        aviso(tb, "[bg] Tarea cancelada; no se creó ningún archivo de salida.");
    } else {
        aviso(tb, "[bg] Error: %s", err[0] ? err : "falló la tarea");
    }
    tb->estado = TB_TERMINADA;                          /* se publica DESPUÉS del mensaje */
    pthread_cond_broadcast(&tb->cv_fin);
    pthread_mutex_unlock(&tb->mu);
    return NULL;
}

/* Copia byte a byte (pread/write) el archivo a un temporal anónimo. */
static int tomar_snapshot(int fd_origen, uint64_t *bytes) {
    char plantilla[] = "/tmp/eafit_snap_XXXXXX";
    int fd = mkstemp(plantilla);
    if (fd == -1) return -1;
    unlink(plantilla);                                  /* anónimo: desaparece al cerrar el fd */

    char *buf = (char *)malloc(65536);
    if (!buf) { close(fd); errno = ENOMEM; return -1; }
    uint64_t off = 0;
    for (;;) {
        ssize_t r = pread(fd_origen, buf, 65536, (off_t)off);
        if (r < 0) { if (errno == EINTR) continue; int e = errno; free(buf); close(fd); errno = e; return -1; }
        if (r == 0) break;
        size_t hecho = 0;
        while (hecho < (size_t)r) {
            ssize_t w = write(fd, buf + hecho, (size_t)r - hecho);
            if (w < 0) { if (errno == EINTR) continue; int e = errno; free(buf); close(fd); errno = e; return -1; }
            hecho += (size_t)w;
        }
        off += (uint64_t)r;
    }
    free(buf);
    *bytes = off;
    return fd;
}

/* Lanza el hilo con TODAS las señales bloqueadas (las atiende solo el hilo UI). */
static int lanzar_hilo(TareaBg *tb) {
    sigset_t todas, previa;
    sigfillset(&todas);
    pthread_sigmask(SIG_BLOCK, &todas, &previa);
    int rc = pthread_create(&tb->hilo, NULL, hilo_tarea, tb);
    pthread_sigmask(SIG_SETMASK, &previa, NULL);
    return rc;
}

static int preparar(TareaBg *tb, TbTipo tipo, const char *origen, const char *destino) {
    tarea_bg_reap(tb);
    pthread_mutex_lock(&tb->mu);
    int ocupada = tb->estado != TB_LIBRE;      /* el hilo de fondo escribe 'estado' bajo mu */
    pthread_mutex_unlock(&tb->mu);
    if (ocupada) {
        fprintf(stderr, "[error] Ya hay una tarea en segundo plano en curso. Use 'e' (estado), 'c' (cancelar) o 'w' (esperar).\n");
        return -1;
    }
    tb->origen = copiar(origen);
    tb->destino = copiar(destino);
    if (!tb->origen || !tb->destino) {
        free(tb->origen); free(tb->destino); tb->origen = tb->destino = NULL;
        fprintf(stderr, "[error] No hay memoria suficiente (malloc).\n");
        return -1;
    }
    tb->tipo = tipo;
    tb->resultado = HUF_OK;
    tb->err[0] = '\0';
    tb->bytes_in = tb->bytes_out = 0;
    tb->ediciones = 0;
    tb->ultimo_hito = 0;
    huf_progreso_iniciar(&tb->prog);
    tb->prog.notificar = al_progresar;
    tb->prog.notificar_ctx = tb;
    clock_gettime(CLOCK_MONOTONIC, &tb->inicio);
    return 0;
}

static void liberar_rutas(TareaBg *tb) {
    free(tb->origen); free(tb->destino);
    tb->origen = tb->destino = NULL;
    tb->tipo = TB_NINGUNA;
}

int tarea_bg_comprimir(TareaBg *tb, int fd_origen, const char *nombre_origen, const char *destino) {
    if (preparar(tb, TB_COMPRIMIR, nombre_origen, destino) != 0) return -1;

    uint64_t bytes = 0;
    int snap = tomar_snapshot(fd_origen, &bytes);      /* sin carrera: solo el hilo UI muta el archivo */
    if (snap == -1) {
        perror("snapshot");
        liberar_rutas(tb);
        return -1;
    }
    tb->fd_snapshot = snap;
    tb->bytes_in = bytes;

    pthread_mutex_lock(&tb->mu);
    tb->estado = TB_CORRIENDO;
    pthread_mutex_unlock(&tb->mu);
    int rc = lanzar_hilo(tb);
    if (rc != 0) {
        pthread_mutex_lock(&tb->mu);
        tb->estado = TB_LIBRE;
        pthread_mutex_unlock(&tb->mu);
        fprintf(stderr, "[error] pthread_create: %s\n", strerror(rc));
        close(snap);
        tb->fd_snapshot = -1;
        liberar_rutas(tb);
        return -1;
    }
    tb->hilo_creado = 1;
    return 0;
}

int tarea_bg_descomprimir(TareaBg *tb, const char *origen, const char *destino) {
    if (preparar(tb, TB_DESCOMPRIMIR, origen, destino) != 0) return -1;
    pthread_mutex_lock(&tb->mu);
    tb->estado = TB_CORRIENDO;
    pthread_mutex_unlock(&tb->mu);
    int rc = lanzar_hilo(tb);
    if (rc != 0) {
        pthread_mutex_lock(&tb->mu);
        tb->estado = TB_LIBRE;
        pthread_mutex_unlock(&tb->mu);
        fprintf(stderr, "[error] pthread_create: %s\n", strerror(rc));
        liberar_rutas(tb);
        return -1;
    }
    tb->hilo_creado = 1;
    return 0;
}

int tarea_bg_activa(TareaBg *tb) {
    pthread_mutex_lock(&tb->mu);
    int a = tb->estado == TB_CORRIENDO;
    pthread_mutex_unlock(&tb->mu);
    return a;
}

void tarea_bg_reap(TareaBg *tb) {
    pthread_mutex_lock(&tb->mu);
    int terminada = tb->estado == TB_TERMINADA;
    pthread_mutex_unlock(&tb->mu);
    if (!terminada) return;
    if (tb->hilo_creado) { pthread_join(tb->hilo, NULL); tb->hilo_creado = 0; }   /* join limpio */
    liberar_rutas(tb);
    pthread_mutex_lock(&tb->mu);
    tb->estado = TB_LIBRE;
    pthread_mutex_unlock(&tb->mu);
}

void tarea_bg_imprimir_estado(TareaBg *tb) {
    tarea_bg_reap(tb);
    pthread_mutex_lock(&tb->mu);
    if (tb->estado != TB_CORRIENDO) {
        printf("No hay ninguna tarea en segundo plano.\n");
        pthread_mutex_unlock(&tb->mu);
        return;
    }
    int pct = huf_progreso_porcentaje(&tb->prog);
    int fase = __atomic_load_n(&tb->prog.fase, __ATOMIC_RELAXED);
    const char *nf = fase == HUF_FASE_CONTEO ? "conteo de frecuencias"
                   : fase == HUF_FASE_CODIFICA ? "codificación de bloques"
                   : fase == HUF_FASE_DECODIFICA ? "decodificación de bloques" : "preparando";
    char barra[21];
    for (int i = 0; i < 20; i++) barra[i] = (i < pct / 5) ? '#' : '.';
    barra[20] = '\0';
    printf("[%s en curso] '%s' -> '%s'\n  [%s] %d%%  (fase: %s, %.1f s)\n",
           tb->tipo == TB_COMPRIMIR ? "compresión" : "descompresión", tb->origen, tb->destino,
           barra, pct, nf, segundos_desde(&tb->inicio));
    if (tb->ediciones > 0) printf("  (ediciones posteriores al snapshot: %d)\n", tb->ediciones);
    pthread_mutex_unlock(&tb->mu);
}

int tarea_bg_cancelar(TareaBg *tb) {
    if (!tarea_bg_activa(tb)) return 0;
    huf_progreso_cancelar(&tb->prog);                   /* atómico: los trabajadores lo ven al siguiente bloque */
    return 1;
}

int tarea_bg_esperar(TareaBg *tb, volatile sig_atomic_t *interrumpido) {
    pthread_mutex_lock(&tb->mu);
    while (tb->estado == TB_CORRIENDO) {
        if (interrumpido && *interrumpido) { pthread_mutex_unlock(&tb->mu); return 1; }
        struct timespec lim;
        clock_gettime(CLOCK_REALTIME, &lim);
        lim.tv_nsec += 200L * 1000000L;                 /* espera con timeout, no sondeo ocupado */
        if (lim.tv_nsec >= 1000000000L) { lim.tv_sec++; lim.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&tb->cv_fin, &tb->mu, &lim);
    }
    pthread_mutex_unlock(&tb->mu);
    tarea_bg_reap(tb);
    return 0;
}

void tarea_bg_finalizar(TareaBg *tb) {
    if (tarea_bg_activa(tb)) {
        printf("Cancelando la tarea en segundo plano antes de cerrar...\n");
        tarea_bg_cancelar(tb);
    }
    pthread_mutex_lock(&tb->mu);
    while (tb->estado == TB_CORRIENDO) pthread_cond_wait(&tb->cv_fin, &tb->mu);
    pthread_mutex_unlock(&tb->mu);
    tarea_bg_reap(tb);
    pthread_cond_destroy(&tb->cv_fin);
    pthread_mutex_destroy(&tb->mu);
}

int tarea_bg_ruta_en_uso(TareaBg *tb, const char *ruta) {
    pthread_mutex_lock(&tb->mu);
    int uso = 0;
    if (tb->estado == TB_CORRIENDO || tb->estado == TB_TERMINADA) {
        uso = tarea_bg_misma_ruta(tb->destino, ruta);
        if (!uso && tb->tipo == TB_DESCOMPRIMIR && tb->estado == TB_CORRIENDO)
            uso = tarea_bg_misma_ruta(tb->origen, ruta);   /* el .huf se está leyendo */
    }
    pthread_mutex_unlock(&tb->mu);
    return uso;
}

void tarea_bg_notificar_edicion(TareaBg *tb) {
    pthread_mutex_lock(&tb->mu);
    if (tb->estado == TB_CORRIENDO && tb->tipo == TB_COMPRIMIR) tb->ediciones++;
    pthread_mutex_unlock(&tb->mu);
}
