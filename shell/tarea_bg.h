#ifndef TAREA_BG_H
#define TAREA_BG_H

/**
 * Módulo: Tarea en segundo plano del editor (Parcial 2 - Alternativa 1)
 * ============================================================================
 * Ejecuta la compresión/descompresión Huffman en un hilo propio (background
 * worker) para que el ciclo interactivo del editor NO se bloquee, y expone
 * progreso en tiempo real, cancelación y cierre limpio.
 *
 * PROTOCOLO DE CONCURRENCIA EDITOR <-> TAREA
 * ----------------------------------------------------------------------------
 * Hilos:  UI (el que lee comandos)  y  TAREA (1 hilo coordinador + su pool).
 *
 * 1. AISLAMIENTO POR SNAPSHOT (comprimir). Al iniciar, el hilo UI copia el
 *    archivo abierto (bytes tal como están en disco) a un archivo temporal
 *    anónimo (mkstemp + unlink) y la tarea comprime ESA copia. El buffer en
 *    memoria y el archivo del usuario nunca se comparten con la tarea, así
 *    que 'a', 'd', 'i' siguen funcionando durante la compresión sin leer
 *    datos a medias (sin lecturas rotas por lseek/write/ftruncate).
 *    El .huf contiene exactamente la versión del archivo al momento del
 *    snapshot; las ediciones posteriores se cuentan y se avisan.
 *
 * 2. RESERVA DE RUTAS (destino). La ruta de salida queda "reservada" mientras
 *    la tarea vive: 'o' sobre esa ruta, otro 'z'/'x' hacia ella, o apuntar la
 *    salida al archivo abierto son rechazados (tarea_bg_ruta_en_uso).
 *    Además la salida se escribe a un temporal y se publica con rename().
 *
 * 3. UNA TAREA A LA VEZ. El estado (LIBRE/CORRIENDO/TERMINADA), el resultado
 *    y los mensajes están protegidos por un mutex; 'w' y el cierre esperan con
 *    variable de condición (sin espera activa).
 *
 * 4. SEÑALES. Los hilos de la tarea y del pool bloquean todas las señales;
 *    SIGINT/SIGTERM las recibe solo el hilo UI, que cancela la tarea, hace
 *    join y libera todo antes de salir.
 */

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "huffman.h"

#define PROMPT_EDITOR "editor> "

typedef enum { TB_LIBRE = 0, TB_CORRIENDO, TB_TERMINADA } TbEstado;
typedef enum { TB_NINGUNA = 0, TB_COMPRIMIR, TB_DESCOMPRIMIR } TbTipo;

typedef struct {
    pthread_mutex_t mu;      /* protege todo lo que sigue (salvo 'prog', atómico) */
    pthread_cond_t cv_fin;   /* se señala cuando estado pasa a TERMINADA */
    TbEstado estado;
    TbTipo tipo;
    pthread_t hilo;
    int hilo_creado;
    HufProgreso prog;
    char *origen, *destino;
    int fd_snapshot;
    int resultado;           /* HUF_OK / HUF_ERROR / HUF_CANCELADO */
    char err[256];
    uint64_t bytes_in, bytes_out;
    int ediciones;           /* ediciones del documento durante una compresión */
    int ultimo_hito;
    int es_tty;
    struct timespec inicio;
} TareaBg;

void tarea_bg_inicializar(TareaBg *tb);

/* Inicia compresión del archivo abierto (fd). Retorna 0 o -1 (mensaje en stderr). */
int tarea_bg_comprimir(TareaBg *tb, int fd_origen, const char *nombre_origen, const char *destino);
/* Inicia descompresión de 'origen' (.huf) hacia 'destino'. */
int tarea_bg_descomprimir(TareaBg *tb, const char *origen, const char *destino);

int  tarea_bg_activa(TareaBg *tb);                  /* 1 si hay una tarea corriendo */
void tarea_bg_reap(TareaBg *tb);                    /* join + limpieza si ya terminó */
void tarea_bg_imprimir_estado(TareaBg *tb);         /* comando 'e' */
int  tarea_bg_cancelar(TareaBg *tb);                /* comando 'c': pide cancelar; 1 si había tarea */
/* Comando 'w': bloquea (cond. variable con timeout para atender señales) hasta que
   termine la tarea. Retorna 1 si se interrumpió por señal (flag != 0). */
int  tarea_bg_esperar(TareaBg *tb, volatile sig_atomic_t *interrumpido);
void tarea_bg_finalizar(TareaBg *tb);               /* cancelar + join + liberar (cierre del editor) */

int  tarea_bg_ruta_en_uso(TareaBg *tb, const char *ruta);   /* 1 si la tarea reservó esa ruta */
void tarea_bg_notificar_edicion(TareaBg *tb);       /* el documento cambió durante una compresión */
int  tarea_bg_misma_ruta(const char *a, const char *b);     /* igual por nombre o por (dev,inode) */

#endif
