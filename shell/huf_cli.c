/**
 * huf - Utilidad de línea de comandos del compresor Huffman concurrente.
 * Sirve para probar el módulo de forma independiente del editor (script de
 * pruebas, benchmarks, valgrind/TSan) y como evidencia para la sustentación.
 *
 *   huf c <entrada> <salida.huf> [hilos] [tam_bloque_bytes]
 *   huf d <entrada.huf> <salida>  [hilos]
 */
#include "huffman.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void uso(void) {
    fprintf(stderr,
        "Uso:\n"
        "  huf c <entrada> <salida.huf> [hilos] [tam_bloque]\n"
        "  huf d <entrada.huf> <salida> [hilos]\n");
}

int main(int argc, char **argv) {
    if (argc < 4 || (strcmp(argv[1], "c") != 0 && strcmp(argv[1], "d") != 0)) { uso(); return 2; }

    HufOpciones op = {0, 0, 0, 0};
    const char *lento = getenv("EAFIT_HUF_RETARDO_MS");
    if (lento) op.retardo_ms = (unsigned)strtoul(lento, NULL, 10);
    if (argc >= 5) op.hilos = atoi(argv[4]);
    if (argc >= 6 && argv[1][0] == 'c') op.tam_bloque = (size_t)strtoull(argv[5], NULL, 10);

    HufProgreso prog;
    huf_progreso_iniciar(&prog);
    char err[256] = "";
    uint64_t out = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int rc = (argv[1][0] == 'c')
        ? huf_comprimir_archivo(argv[2], argv[3], &op, &prog, &out, err, sizeof(err))
        : huf_descomprimir_archivo(argv[2], argv[3], &op, &prog, &out, err, sizeof(err));

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    if (rc != HUF_OK) {
        fprintf(stderr, "huf: %s\n", err[0] ? err : "error");
        return 1;
    }
    printf("%s OK: %llu bytes de salida en %.1f ms\n",
           argv[1][0] == 'c' ? "Compresión" : "Descompresión", (unsigned long long)out, ms);
    return 0;
}
