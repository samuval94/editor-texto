#ifndef HUFFMAN_H
#define HUFFMAN_H

/**
 * Módulo: Compresor Huffman Concurrente (Parcial 2 - Alternativa 1)
 * ============================================================================
 * Compresión / descompresión por bloques con un pool de hilos (pthread).
 *
 * COMPRESIÓN (dos fases, ambas paralelas):
 *   Fase 1 - Conteo de frecuencias: los N hilos del pool toman bloques del
 *            archivo (pread), cuentan en un histograma LOCAL (reducción
 *            local, sin contención) y al terminar lo suman al global bajo
 *            mutex.
 *   (El hilo llamador construye el árbol de Huffman global y los códigos
 *    canónicos entre las dos fases.)
 *   Fase 2 - Codificación: los hilos codifican bloques en paralelo; el hilo
 *            llamador actúa de COORDINADOR/ESCRITOR y escribe los bloques en
 *            el archivo de salida en orden secuencial estricto, usando una
 *            ventana acotada de slots (productor-consumidor con mutex +
 *            variables de condición, sin espera activa).
 *
 * DESCOMPRESIÓN: misma tubería ordenada; los bloques son independientes
 * (cada uno es byte-aligned), se decodifican en paralelo y se verifica el
 * CRC-32 de cada bloque contra el original.
 *
 * FORMATO (.huf, enteros little-endian):
 *   "HUF1" | u32 tam_bloque | u64 tam_original | u32 n_bloques |
 *   u8 longitudes_de_codigo[256] | n_bloques x { u32 tam_comprimido, u32 crc32 }
 *   | payload del bloque 0 | payload del bloque 1 | ...
 *
 * La salida se escribe primero a un archivo temporal en el mismo directorio
 * y se publica con rename() solo si todo salió bien (atómico: nunca queda un
 * .huf a medias; si se cancela o falla, el temporal se borra con unlink()).
 *
 * Seguridad entre hilos: el progreso y la cancelación (HufProgreso) se
 * pueden leer/escribir desde otro hilo (campos atómicos).
 */

#include <stddef.h>
#include <stdint.h>

#define HUF_OK         0
#define HUF_ERROR     -1
#define HUF_CANCELADO -2

#define HUF_FASE_INACTIVA 0
#define HUF_FASE_CONTEO   1
#define HUF_FASE_CODIFICA 2
#define HUF_FASE_DECODIFICA 3

typedef struct {
    uint64_t total;   /* Unidades totales de trabajo (bytes procesados en todas las fases). */
    uint64_t hecho;   /* Unidades completadas. ATÓMICO: leer con huf_progreso_porcentaje(). */
    int cancelar;     /* Poner en 1 (atómico) desde cualquier hilo para cancelar. */
    int fase;         /* HUF_FASE_* (informativo). */
    /* Notificación dirigida por eventos (sin sondeo): si no es NULL, se invoca
       desde el hilo trabajador que haga cruzar cada múltiplo de 10 % (10,20,..,90).
       Debe ser rápida y thread-safe. */
    void (*notificar)(void *ctx, int porcentaje);
    void *notificar_ctx;
} HufProgreso;

typedef struct {
    int hilos;            /* Hilos trabajadores del pool (<=0: automático por nº de CPUs). */
    size_t tam_bloque;    /* Bytes por bloque al comprimir (0: 256 KiB). */
    size_t ventana;       /* Bloques en vuelo máximo (0: 4 * hilos). Acota la memoria. */
    unsigned retardo_ms;  /* Pausa artificial por bloque (solo demos/pruebas de cancelación; 0 = ninguna). */
} HufOpciones;

void huf_progreso_iniciar(HufProgreso *p);
void huf_progreso_cancelar(HufProgreso *p);
int  huf_progreso_porcentaje(const HufProgreso *p);   /* 0..100 */
int  huf_progreso_cancelado(const HufProgreso *p);

/**
 * Comprime el contenido del descriptor 'fd_in' (se lee con pread(); no se
 * modifica su offset ni se cierra) hacia 'ruta_out'.
 * Retorna HUF_OK, HUF_ERROR (mensaje en 'err') o HUF_CANCELADO.
 * 'bytes_out' (opcional) recibe el tamaño del archivo comprimido.
 */
int huf_comprimir_fd(int fd_in, const char *ruta_out, const HufOpciones *op,
                     HufProgreso *prog, uint64_t *bytes_out,
                     char *err, size_t err_sz);

/** Comprime el archivo 'ruta_in' hacia 'ruta_out' (envoltura sobre huf_comprimir_fd). */
int huf_comprimir_archivo(const char *ruta_in, const char *ruta_out, const HufOpciones *op,
                          HufProgreso *prog, uint64_t *bytes_out,
                          char *err, size_t err_sz);

/** Descomprime 'ruta_in' (.huf) hacia 'ruta_out', verificando CRC-32 por bloque. */
int huf_descomprimir_archivo(const char *ruta_in, const char *ruta_out, const HufOpciones *op,
                             HufProgreso *prog, uint64_t *bytes_out,
                             char *err, size_t err_sz);

#endif
