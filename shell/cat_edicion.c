#include "shell.h"
#include "modo_comando.h"
#include <stdio.h>

/**
 * ====================================================================================
 * CATEGORÍA: Edición de Texto (cat_edicion.c)
 * ====================================================================================
 * Este módulo integra el Editor de Texto del proyecto como una nueva categoría
 * del shell educativo: "edicion".
 *
 * DIFERENCIA ARQUITECTÓNICA CON EL RESTO DEL SHELL
 * --------------------------------------------------------------------------
 * Todos los demás comandos del shell (d_create, m_sbrk, p_fork, etc.) siguen
 * el patrón "una syscall (o un grupo pequeño) por comando": el usuario escribe
 * el comando, la función handler ejecuta sus llamadas al sistema, imprime el
 * resultado y retorna inmediatamente el control al prompt 'eafitOS>'.
 *
 * El editor de texto, en cambio, es intrínsecamente un programa con su PROPIO
 * ciclo interactivo (REPL) de comandos internos (o/p/a/d/i/s/q) que puede
 * durar una sesión completa de edición. Por eso NO encaja como un comando de
 * "una sola syscall" dentro de las categorías existentes (datos, memoria,
 * monitoreo, utilidades): se ejecuta un bucle anidado que toma el control de
 * STDIN/STDOUT hasta que el usuario escribe 'q' (o EOF), y solo entonces
 * retorna el control al bucle REPL principal del shell. Esta es la razón por
 * la que se creó una categoría nueva ("edicion") en vez de forzar el editor
 * dentro de una categoría existente.
 *
 * Syscalls explicadas (implementadas en estructura.c y modo_comando.c):
 * 1. open(2): abre o crea el archivo de trabajo (O_RDWR | O_CREAT, 0644).
 * 2. read(2) / lseek(2): cargan el contenido del archivo a un buffer
 *    dinámico en memoria (lista enlazada de líneas/palabras, malloc/free).
 * 3. write(2) / lseek(2): sincronizan cada modificación (a/d/i) de vuelta
 *    al disco: 'a' usa lseek(SEEK_END)+write (apéndice eficiente); 'd' e
 *    'i' reescriben el archivo completo con lseek(SEEK_SET)+write.
 * 4. ftruncate(2): recorta el archivo al tamaño exacto tras 'd' (borrado
 *    de línea), evitando bytes residuales de una versión más larga.
 * 5. close(2): libera el descriptor de archivo al terminar la sesión
 *    ('q' o EOF), junto con toda la memoria dinámica de la estructura.
 */
int cmd_editor(int argc, char **argv) {
    const char *archivo = (argc >= 2) ? argv[1] : NULL;

    printf(COLOR_INFO "[edicion] Iniciando sesión de edición interactiva.\n" COLOR_RESET);
    printf(COLOR_INFO "[edicion] El shell queda en pausa hasta que se escriba 'q' dentro del editor.\n\n" COLOR_RESET);

    int resultado = modo_comando_ejecutar(archivo);

    printf(COLOR_INFO "\n[edicion] Sesión de edición finalizada. Regresando al shell.\n" COLOR_RESET);
    return resultado;
}
