#ifndef MODO_COMANDO_H
#define MODO_COMANDO_H

/**
 * Módulo: Modo de Comandos (CLI)
 * ============================================================================
 * Implementa el ciclo interactivo de edición exigido por la rúbrica del
 * proyecto (Sección 3 "Comandos Base CLI" y Sección 4, nivel "2 - Parejas"):
 *
 *   o [archivo]      Abre/crea un archivo.
 *   p [n]            Imprime la línea n, o todo el archivo si se omite.
 *   a [texto]        Añade una línea nueva al final del archivo.
 *   d [n]            Borra la línea n.
 *   i [n] [texto]    Inserta una línea nueva en la posición n.
 *   s [palabra]      Busca una palabra en el documento.
 *   q                Cierra el archivo (fd) y termina, sin fugas de memoria.
 *
 * Toda manipulación del archivo en disco se hace exclusivamente con
 * llamadas al sistema POSIX de bajo nivel (open, read, write, lseek,
 * ftruncate, close); fopen/fread/fwrite/fclose no se usan en ningún punto.
 * Solo se emplea E/S estándar (printf/fgets) para leer los comandos del
 * usuario desde STDIN e imprimir mensajes de la interfaz en STDOUT.
 */

/**
 * Ejecuta el ciclo interactivo de comandos.
 *
 * - archivo_inicial: si no es NULL, se abre automáticamente al arrancar
 *   (equivalente a ejecutar 'o <archivo_inicial>' como primer comando).
 *   Puede ser NULL; en ese caso el usuario debe usar 'o <archivo>' primero.
 *
 * Retorna 0 al terminar normalmente (comando 'q' o EOF en STDIN).
 */
int modo_comando_ejecutar(const char *archivo_inicial);

#endif
