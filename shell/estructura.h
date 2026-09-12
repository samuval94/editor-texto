#ifndef ESTRUCTURA_H
#define ESTRUCTURA_H

#include <stddef.h> // Para tipos base como size_t y constantes como NULL
#include <stdio.h>  // Para operaciones de entrada/salida estándar

/**
 * Estructura para representar una palabra (letras continuas o espacios).
 * Forma parte de una lista enlazada dentro de una línea.
 */
typedef struct NodoPalabra {
    char *texto;
    size_t longitud;
    size_t capacidad;
    struct NodoPalabra *anterior;
    struct NodoPalabra *siguiente;
} NodoPalabra;

/**
 * Estructura para representar una línea de texto individual.
 * Ahora contiene una lista ligada de palabras.
 */
typedef struct NodoLinea {
    NodoPalabra *palabras_cabeza;
    NodoPalabra *palabras_cola;
    size_t longitud;             // Longitud actual de la línea
    struct NodoLinea *anterior;  // Puntero a la línea anterior
    struct NodoLinea *siguiente; // Puntero a la línea siguiente
} NodoLinea;

/**
 * Estructura para representar el estructura de texto completo.
 */
typedef struct {
    NodoLinea *cabeza;         // Primera línea del texto
    NodoLinea *cola;           // Última línea del texto
    int totalLineas;           // Contador del total de líneas
} EstructuraTexto;

// Funciones
EstructuraTexto* estructura_crear();
void estructura_destruir(EstructuraTexto *alm);
NodoLinea* estructura_insertar_linea(EstructuraTexto *alm, NodoLinea *despues_de);
void estructura_eliminar_linea(EstructuraTexto *alm, NodoLinea *linea);
void linea_insertar_caracter(NodoLinea *linea, size_t pos, char c);
void linea_eliminar_caracter(NodoLinea *linea, size_t pos);
int estructura_guardar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out);
int estructura_cargar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out);

// Buscar palabra
int estructura_buscar_palabra(EstructuraTexto *alm, const char *palabra, NodoLinea **linea_encontrada, int *pos_x, int *pos_y);

// ============================================================================
// EXTENSIONES PARA EL MODO DE COMANDOS CLI (o/p/a/d/i/s/q)
// ============================================================================

/**
 * Reemplaza el contenido completo de una línea por una nueva cadena de texto.
 * Libera las palabras anteriores y reconstruye la lista de NodoPalabra a partir
 * de 'texto'. Se usa para insertar líneas completas (comandos 'a' e 'i').
 */
void linea_asignar_texto(NodoLinea *linea, const char *texto);

/**
 * Copia el contenido de una línea (concatenando sus NodoPalabra) a un buffer
 * proporcionado por el llamador, respetando el tamaño máximo 'bufsize'.
 */
void estructura_obtener_texto_linea(NodoLinea *linea, char *buffer, size_t bufsize);

/**
 * Obtiene el puntero al nodo de la línea número 'n' (1-indexada).
 * Retorna NULL si 'n' está fuera de rango.
 */
NodoLinea* estructura_obtener_linea(EstructuraTexto *alm, int n);

/**
 * Busca todas las ocurrencias de 'palabra' como subcadena en cada línea del
 * documento. Llena 'lineas_encontradas' (1-indexadas) hasta 'max_resultados'.
 * Retorna el número total de líneas donde aparece la palabra.
 */
int estructura_buscar_todas(EstructuraTexto *alm, const char *palabra, int *lineas_encontradas, int max_resultados);

/**
 * Carga el contenido de un archivo ya abierto (descriptor 'fd') hacia la
 * estructura en memoria. A diferencia de estructura_cargar_archivo, esta
 * variante NO abre ni cierra el descriptor: opera sobre uno ya gestionado
 * por el llamador (el modo de comandos mantiene un único fd por sesión,
 * tal como exige el reto de nivel 1: "gestión básica de descriptores de
 * archivo"). Hace lseek(fd, 0, SEEK_SET) antes de leer.
 * Retorna 0 en éxito, -1 en error.
 */
int estructura_cargar_desde_fd(EstructuraTexto *alm, int fd);

/**
 * Reescribe el archivo completo en disco (sobre el descriptor 'fd' ya
 * abierto) reflejando el estado actual en memoria. Demuestra el uso
 * explícito de lseek() y ftruncate(): se posiciona al inicio con
 * lseek(SEEK_SET), escribe el contenido nuevo completo y luego recorta
 * cualquier byte sobrante de una versión anterior más larga con
 * ftruncate(). Se usa tras 'd' (borrar línea) e 'i' (insertar línea),
 * operaciones que desplazan bytes y pueden acortar o alargar el archivo.
 * Retorna 0 en éxito, -1 en error (con errno asignado por la syscall fallida).
 */
int estructura_sincronizar_reescribir(EstructuraTexto *alm, int fd);

/**
 * Añade una única línea de texto al final del archivo en disco (sobre el
 * descriptor 'fd' ya abierto) de forma eficiente, sin reescribir el
 * archivo completo: lseek(SEEK_END) + write(). Se usa para el comando
 * 'a', que solo agrega bytes al final (no desplaza nada existente).
 * Retorna 0 en éxito, -1 en error.
 */
int estructura_sincronizar_append(int fd, const char *texto);

#endif
