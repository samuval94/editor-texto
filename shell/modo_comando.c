#include "modo_comando.h"
#include "estructura.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_LINEA_ENTRADA 4096
#define MAX_RESULTADOS_BUSQUEDA 256

/**
 * Estado de la sesión de edición interactiva.
 *
 * Se mantiene UN ÚNICO descriptor de archivo (fd) abierto durante toda la
 * sesión, desde el comando 'o' hasta el comando 'q' (o hasta que se abra
 * otro archivo con 'o', que primero cierra el anterior). Esto demuestra la
 * "gestión básica de descriptores de archivo" exigida por el Reto Técnico
 * de nivel 1, y evita abrir/cerrar el archivo en cada operación.
 */
typedef struct {
    EstructuraTexto *estructura; /* Buffer dinámico en memoria (lista enlazada de líneas/palabras) */
    char *nombre_archivo;        /* Ruta del archivo actualmente abierto (o NULL) */
    int fd;                      /* Descriptor de archivo actual (-1 si no hay archivo abierto) */
} EstadoComando;

/* Duplica una cadena con malloc (evitamos strdup para mantener compatibilidad estricta con -std=c99). */
static char* duplicar_cadena(const char *origen) {
    size_t len = strlen(origen);
    char *copia = (char*)malloc(len + 1);
    if (!copia) return NULL;
    memcpy(copia, origen, len + 1);
    return copia;
}

static void estado_inicializar(EstadoComando *ec) {
    ec->estructura = NULL;
    ec->nombre_archivo = NULL;
    ec->fd = -1;
}

/* Cierra el archivo actual (si existe) y libera toda la memoria dinámica asociada. */
static void estado_cerrar_archivo(EstadoComando *ec) {
    if (ec->fd != -1) {
        /* --- CALL SYSTEM: close() --- */
        if (close(ec->fd) == -1) {
            perror("close");
        }
        ec->fd = -1;
    }
    if (ec->estructura) {
        estructura_destruir(ec->estructura);
        ec->estructura = NULL;
    }
    if (ec->nombre_archivo) {
        free(ec->nombre_archivo);
        ec->nombre_archivo = NULL;
    }
}

static int requiere_archivo_abierto(const EstadoComando *ec) {
    if (ec->fd == -1 || !ec->estructura) {
        fprintf(stderr, "[error] No hay ningún archivo abierto. Use: o <archivo>\n");
        return 0;
    }
    return 1;
}

/* ============================================================================
 * o [archivo] - Abre un archivo en disco. Si no existe, lo crea.
 * Syscalls: open() con O_RDWR | O_CREAT, mode_t
 * ========================================================================= */
static int cmd_o(EstadoComando *ec, const char *archivo) {
    if (!archivo || strlen(archivo) == 0) {
        fprintf(stderr, "[error] Uso: o <archivo>\n");
        return -1;
    }

    /* Si ya había un archivo abierto en esta sesión, lo cerramos primero
       (cierra el fd y libera el buffer dinámico) para no dejar fugas. */
    estado_cerrar_archivo(ec);

    /* --- CALL SYSTEM: open() ---
       O_RDWR: permite tanto leer como escribir sobre el mismo descriptor.
       O_CREAT: si el archivo no existe, se crea.
       0644 (mode_t): permisos rw-r--r-- aplicados solo si se creó el archivo. */
    int fd = open(archivo, O_RDWR | O_CREAT, (mode_t)0644);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    ec->fd = fd;
    ec->nombre_archivo = duplicar_cadena(archivo);
    if (!ec->nombre_archivo) {
        fprintf(stderr, "[error] No hay memoria suficiente (malloc).\n");
        close(ec->fd);
        ec->fd = -1;
        return -1;
    }

    ec->estructura = estructura_crear();
    if (!ec->estructura) {
        fprintf(stderr, "[error] No hay memoria suficiente (malloc).\n");
        close(ec->fd);
        ec->fd = -1;
        free(ec->nombre_archivo);
        ec->nombre_archivo = NULL;
        return -1;
    }

    if (estructura_cargar_desde_fd(ec->estructura, ec->fd) == -1) {
        perror("read");
        /* No es necesariamente fatal: el archivo queda abierto y vacío en memoria. */
    }

    printf("Archivo '%s' abierto (fd=%d). Líneas cargadas: %d\n",
           ec->nombre_archivo, ec->fd, ec->estructura->totalLineas);
    return 0;
}

/* ============================================================================
 * p [n] - Imprime la línea n. Sin parámetros, imprime todo el archivo.
 * Syscalls: write() (FD 1). El contenido se imprime con write() de bajo
 * nivel (en vez de printf) para alinear con la recomendación de la rúbrica.
 * ========================================================================= */
static int cmd_p(EstadoComando *ec, const char *arg) {
    if (!requiere_archivo_abierto(ec)) return -1;

    long n = -1;
    if (arg && strlen(arg) > 0) {
        char *fin = NULL;
        errno = 0;
        n = strtol(arg, &fin, 10);
        if (fin == arg || *fin != '\0' || n <= 0) {
            fprintf(stderr, "[error] Número de línea inválido: '%s'\n", arg);
            return -1;
        }
    }

    char linea_txt[4096];
    if (n == -1) {
        /* Imprime todo el archivo, recorriendo línea por línea */
        NodoLinea *actual = ec->estructura->cabeza;
        int i = 1;
        while (actual != NULL) {
            estructura_obtener_texto_linea(actual, linea_txt, sizeof(linea_txt));
            char encabezado[32];
            int len_enc = snprintf(encabezado, sizeof(encabezado), "%4d| ", i);
            /* --- CALL SYSTEM: write() (STDOUT_FILENO / FD 1) --- */
            write(STDOUT_FILENO, encabezado, (size_t)len_enc);
            write(STDOUT_FILENO, linea_txt, strlen(linea_txt));
            write(STDOUT_FILENO, "\n", 1);
            actual = actual->siguiente;
            i++;
        }
    } else {
        NodoLinea *linea = estructura_obtener_linea(ec->estructura, (int)n);
        if (!linea) {
            fprintf(stderr, "[error] La línea %ld no existe (el archivo tiene %d línea(s)).\n",
                    n, ec->estructura->totalLineas);
            return -1;
        }
        estructura_obtener_texto_linea(linea, linea_txt, sizeof(linea_txt));
        /* --- CALL SYSTEM: write() (STDOUT_FILENO / FD 1) --- */
        write(STDOUT_FILENO, linea_txt, strlen(linea_txt));
        write(STDOUT_FILENO, "\n", 1);
    }
    return 0;
}

/* ============================================================================
 * a [texto] - Añade el texto provisto como una nueva línea al final.
 * Syscalls: lseek() (SEEK_END), write()
 * ========================================================================= */
static int cmd_a(EstadoComando *ec, const char *texto) {
    if (!requiere_archivo_abierto(ec)) return -1;
    if (!texto) texto = "";

    NodoLinea *nueva = estructura_insertar_linea(ec->estructura, ec->estructura->cola);
    linea_asignar_texto(nueva, texto);

    if (estructura_sincronizar_append(ec->fd, texto) == -1) {
        perror("write");
        return -1;
    }
    printf("Línea añadida al final (ahora es la línea %d).\n", ec->estructura->totalLineas);
    return 0;
}

/* ============================================================================
 * d [n] - Borra la línea n (desplazando los bytes posteriores y truncando).
 * Syscalls: read(), write(), lseek(), ftruncate()
 * ========================================================================= */
static int cmd_d(EstadoComando *ec, const char *arg) {
    if (!requiere_archivo_abierto(ec)) return -1;
    if (!arg || strlen(arg) == 0) {
        fprintf(stderr, "[error] Uso: d <n>\n");
        return -1;
    }
    char *fin = NULL;
    errno = 0;
    long val = strtol(arg, &fin, 10);
    if (fin == arg || *fin != '\0' || val <= 0) {
        fprintf(stderr, "[error] Número de línea inválido: '%s'\n", arg);
        return -1;
    }
    int n = (int)val;
    NodoLinea *linea = estructura_obtener_linea(ec->estructura, n);
    if (!linea) {
        fprintf(stderr, "[error] La línea %d no existe (el archivo tiene %d línea(s)).\n",
                n, ec->estructura->totalLineas);
        return -1;
    }
    estructura_eliminar_linea(ec->estructura, linea);

    if (estructura_sincronizar_reescribir(ec->estructura, ec->fd) == -1) {
        perror("sincronizar (d)");
        return -1;
    }
    printf("Línea %d eliminada.\n", n);
    return 0;
}

/* ============================================================================
 * i [n] [texto] - Inserta texto en la línea n, desplazando el resto.
 * Requisito de pareja: manejo avanzado de lseek() y buffers dinámicos.
 * ========================================================================= */
static int cmd_i(EstadoComando *ec, char *arg) {
    if (!requiere_archivo_abierto(ec)) return -1;
    if (!arg || strlen(arg) == 0) {
        fprintf(stderr, "[error] Uso: i <n> <texto>\n");
        return -1;
    }

    char *fin_num = NULL;
    errno = 0;
    long val = strtol(arg, &fin_num, 10);
    if (fin_num == arg || val <= 0) {
        fprintf(stderr, "[error] Uso: i <n> <texto>  (n debe ser un entero positivo)\n");
        return -1;
    }
    int n = (int)val;
    char *texto = fin_num;
    while (*texto == ' ') texto++;
    if (strlen(texto) == 0) {
        fprintf(stderr, "[error] Falta el texto a insertar. Uso: i <n> <texto>\n");
        return -1;
    }

    int total = ec->estructura->totalLineas;
    NodoLinea *despues_de;
    if (n <= 1) {
        despues_de = NULL;                    /* insertar como nueva primera línea */
    } else if (n > total) {
        despues_de = ec->estructura->cola;    /* insertar al final */
    } else {
        despues_de = estructura_obtener_linea(ec->estructura, n - 1);
    }

    NodoLinea *nueva = estructura_insertar_linea(ec->estructura, despues_de);
    linea_asignar_texto(nueva, texto);

    if (estructura_sincronizar_reescribir(ec->estructura, ec->fd) == -1) {
        perror("sincronizar (i)");
        return -1;
    }
    printf("Línea insertada en la posición %d.\n", n);
    return 0;
}

/* ============================================================================
 * s [palabra] - Búsqueda simple de una palabra en el documento.
 * ========================================================================= */
static int cmd_s(EstadoComando *ec, const char *palabra) {
    if (!requiere_archivo_abierto(ec)) return -1;
    if (!palabra || strlen(palabra) == 0) {
        fprintf(stderr, "[error] Uso: s <palabra>\n");
        return -1;
    }
    int resultados[MAX_RESULTADOS_BUSQUEDA];
    int n = estructura_buscar_todas(ec->estructura, palabra, resultados, MAX_RESULTADOS_BUSQUEDA);
    if (n == 0) {
        printf("La palabra '%s' no se encontró en el archivo.\n", palabra);
    } else {
        int mostrar = (n < MAX_RESULTADOS_BUSQUEDA) ? n : MAX_RESULTADOS_BUSQUEDA;
        printf("'%s' encontrada en %d línea(s): ", palabra, n);
        for (int i = 0; i < mostrar; i++) {
            printf("%d%s", resultados[i], (i < mostrar - 1) ? ", " : "");
        }
        printf("\n");
    }
    return 0;
}

/* ============================================================================
 * Banner de bienvenida (arte ASCII "EAFIT de texto") mostrado al iniciar
 * el modo de comandos. Se guarda como un arreglo de líneas (en vez de un
 * único literal gigante) para que el código fuente siga siendo legible.
 * ========================================================================= */
static const char *BANNER_EAFIT[] = {
"                                                 /$$$$$$$$  /$$$$$$  /$$$$$$$$ /$$$$$$ /$$$$$$$$                                        ",
"                                                | $$_____/ /$$__  $$| $$_____/|_  $$_/|__  $$__/                                        ",
"                                                | $$      | $$  \\ $$| $$        | $$     | $$                                           ",
"                                                | $$$$$   | $$$$$$$$| $$$$$     | $$     | $$                                           ",
"                                                | $$__/   | $$__  $$| $$__/     | $$     | $$                                           ",
"                                                | $$      | $$  | $$| $$        | $$     | $$                                           ",
"                                                | $$$$$$$$| $$  | $$| $$       /$$$$$$   | $$                                           ",
"                                                |________/|__/  |__/|__/      |______/   |__/                                           ",
"",
"",
"",
"                 /$$ /$$   /$$                                     /$$                   /$$                           /$$              ",
"                | $$|__/  | $$                                    | $$                  | $$                          | $$              ",
"  /$$$$$$   /$$$$$$$ /$$ /$$$$$$    /$$$$$$   /$$$$$$         /$$$$$$$  /$$$$$$        /$$$$$$    /$$$$$$  /$$   /$$ /$$$$$$    /$$$$$$ ",
" /$$__  $$ /$$__  $$| $$|_  $$_/   /$$__  $$ /$$__  $$       /$$__  $$ /$$__  $$      |_  $$_/   /$$__  $$|  $$ /$$/|_  $$_/   /$$__  $$",
"| $$$$$$$$| $$  | $$| $$  | $$    | $$  \\ $$| $$  \\__/      | $$  | $$| $$$$$$$$        | $$    | $$$$$$$$ \\  $$$$/   | $$    | $$  \\ $$",
"| $$_____/| $$  | $$| $$  | $$ /$$| $$  | $$| $$            | $$  | $$| $$_____/        | $$ /$$| $$_____/  >$$  $$   | $$ /$$| $$  | $$",
"|  $$$$$$$|  $$$$$$$| $$  |  $$$$/|  $$$$$$/| $$            |  $$$$$$$|  $$$$$$$        |  $$$$/|  $$$$$$$ /$$/\\  $$  |  $$$$/|  $$$$$$/",
" \\_______/ \\_______/|__/   \\___/   \\______/ |__/             \\_______/ \\_______/         \\___/   \\_______/|__/  \\__/   \\___/   \\______/"
};

static void mostrar_banner_bienvenida(void) {
    size_t total = sizeof(BANNER_EAFIT) / sizeof(BANNER_EAFIT[0]);
    for (size_t i = 0; i < total; i++) {
        printf("%s\n", BANNER_EAFIT[i]);
    }
}

static void mostrar_ayuda(void) {
    printf(
        "Comandos disponibles:\n"
        "  o <archivo>       Abre (o crea) un archivo.\n"
        "  p [n]             Imprime la línea n, o todo el archivo si se omite.\n"
        "  a <texto>         Añade <texto> como nueva línea al final.\n"
        "  d <n>             Borra la línea n.\n"
        "  i <n> <texto>     Inserta <texto> como línea n, desplazando el resto.\n"
        "  s <palabra>       Busca <palabra> en el documento (imprime líneas).\n"
        "  q                 Cierra el archivo (fd) y sale del editor.\n"
        "  h | ?             Muestra esta ayuda.\n"
    );
}

int modo_comando_ejecutar(const char *archivo_inicial) {
    EstadoComando ec;
    estado_inicializar(&ec);

    mostrar_banner_bienvenida();
    printf("Editor de Texto EAFIT - Modo de Comandos\n\n");
    mostrar_ayuda();

    if (archivo_inicial && strlen(archivo_inicial) > 0) {
        cmd_o(&ec, archivo_inicial);
    }

    char linea[MAX_LINEA_ENTRADA];
    int salir = 0;

    while (!salir) {
        printf("editor> ");
        fflush(stdout);

        /* --- E/S ESTÁNDAR PERMITIDA: fgets() para leer comandos desde STDIN --- */
        if (fgets(linea, sizeof(linea), stdin) == NULL) {
            printf("\n");
            break; /* EOF (Ctrl+D): terminamos igual que con 'q' */
        }

        size_t len = strlen(linea);
        while (len > 0 && (linea[len - 1] == '\n' || linea[len - 1] == '\r')) {
            linea[--len] = '\0';
        }
        if (len == 0) continue; /* línea vacía: ignorar */

        char comando = linea[0];
        char *resto = linea + 1;
        while (*resto == ' ') resto++;

        switch (comando) {
            case 'o': cmd_o(&ec, resto);  break;
            case 'p': cmd_p(&ec, resto);  break;
            case 'a': cmd_a(&ec, resto);  break;
            case 'd': cmd_d(&ec, resto);  break;
            case 'i': cmd_i(&ec, resto);  break;
            case 's': cmd_s(&ec, resto);  break;
            case 'q': salir = 1;          break;
            case 'h':
            case '?': mostrar_ayuda();    break;
            default:
                fprintf(stderr, "[error] Comando desconocido: '%c'. Escriba 'h' para ver la ayuda.\n", comando);
        }
    }

    /* --- CALL SYSTEM: close() + free() de todo el buffer dinámico ---
       Garantiza cero fugas de memoria y de descriptores de archivo,
       verificable con valgrind. */
    estado_cerrar_archivo(&ec);
    printf("Editor cerrado correctamente (sin fugas de memoria ni descriptores abiertos).\n");
    return 0;
}
