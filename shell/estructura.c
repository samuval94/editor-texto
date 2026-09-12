/* ftruncate() es POSIX, no C99 puro: bajo -std=c99 -pedantic el compilador
   no la declara a menos que pidamos explícitamente las cabeceras POSIX. */
#define _POSIX_C_SOURCE 200809L

#include "estructura.h"
#include <stdlib.h> // Para asignación y liberación de memoria dinámica (malloc, free)
#include <string.h> // Para manipulación y operaciones con cadenas de texto (strlen, strcpy, strstr, etc.)
#include <fcntl.h>  // Para opciones de control de archivos (flags de open como O_WRONLY, O_CREAT, etc.)
#include <unistd.h> // Para acceso a la API POSIX del sistema operativo (system calls: open, read, write, close)
#include <stdio.h>  // Para funciones de entrada y salida formateadas (snprintf, sprintf)

#define SAFE_PRINT(log_out, offset, max_sz, ...) \
    do { \
        if (log_out && offset < max_sz) { \
            int cb = snprintf(log_out + offset, max_sz - offset, __VA_ARGS__); \
            if (cb > 0) offset += cb; \
            if (offset >= max_sz) offset = max_sz - 1; \
        } \
    } while(0)


EstructuraTexto* estructura_crear() {
    EstructuraTexto *alm = (EstructuraTexto*)malloc(sizeof(EstructuraTexto));
    alm->cabeza = NULL; alm->cola = NULL; alm->totalLineas = 0;
    estructura_insertar_linea(alm, NULL);
    return alm;
}

static void liberar_palabras(NodoLinea *linea) {
    NodoPalabra *actual = linea->palabras_cabeza;
    while (actual) {
        NodoPalabra *sig = actual->siguiente;
        free(actual->texto); free(actual);        
        actual = sig;
    }
    linea->palabras_cabeza = NULL; linea->palabras_cola = NULL; linea->longitud = 0;
}

void estructura_destruir(EstructuraTexto *alm) {
    if (!alm) return;
    NodoLinea *actual = alm->cabeza;
    while (actual) {
        NodoLinea *sig = actual->siguiente;
        liberar_palabras(actual);
        free(actual);
        actual = sig;
    }
    free(alm);
}

static void enlazar_nueva_linea(EstructuraTexto *alm, NodoLinea *nueva_linea, NodoLinea *despues_de) {
    if (despues_de == NULL) {
        nueva_linea->siguiente = alm->cabeza; nueva_linea->anterior = NULL;
        if (alm->cabeza) alm->cabeza->anterior = nueva_linea;
        alm->cabeza = nueva_linea;
        if (!alm->cola) alm->cola = nueva_linea;
    } else {
        nueva_linea->siguiente = despues_de->siguiente; nueva_linea->anterior = despues_de;
        despues_de->siguiente = nueva_linea;
        if (nueva_linea->siguiente) nueva_linea->siguiente->anterior = nueva_linea;
        else alm->cola = nueva_linea;
    }
}

NodoLinea* estructura_insertar_linea(EstructuraTexto *alm, NodoLinea *despues_de) {
    NodoLinea *nueva_linea = (NodoLinea*)malloc(sizeof(NodoLinea));
    nueva_linea->palabras_cabeza = NULL; nueva_linea->palabras_cola = NULL; nueva_linea->longitud = 0;
    enlazar_nueva_linea(alm, nueva_linea, despues_de);
    alm->totalLineas++;
    return nueva_linea;
}

void estructura_eliminar_linea(EstructuraTexto *alm, NodoLinea *linea) {
    if (!alm || !linea) return;
    if (linea->anterior) linea->anterior->siguiente = linea->siguiente;
    else alm->cabeza = linea->siguiente;
    if (linea->siguiente) linea->siguiente->anterior = linea->anterior;
    else alm->cola = linea->anterior;
    liberar_palabras(linea); free(linea); alm->totalLineas--;
}

static NodoPalabra* crear_nodo_palabra(const char *texto, size_t len) {
    NodoPalabra *nodo = (NodoPalabra*)malloc(sizeof(NodoPalabra));
    nodo->longitud = len; nodo->capacidad = len + 1;
    nodo->texto = (char*)malloc(nodo->capacidad);
    if (len > 0) strncpy(nodo->texto, texto, len);
    nodo->texto[len] = '\0';
    nodo->anterior = NULL; nodo->siguiente = NULL;
    return nodo;
}

static void agregar_palabra(NodoLinea *linea, const char *texto, size_t len) {
    if (len == 0) return;
    NodoPalabra *nodo = crear_nodo_palabra(texto, len);
    if (!linea->palabras_cabeza) {
        linea->palabras_cabeza = nodo; linea->palabras_cola = nodo;
    } else {
        linea->palabras_cola->siguiente = nodo;
        nodo->anterior = linea->palabras_cola;
        linea->palabras_cola = nodo;
    }
    linea->longitud += len;
}

static const char* agrupar_espacios(NodoLinea *linea, const char *p) {
    const char *inicio = p;
    while (*p == ' ') p++;
    agregar_palabra(linea, inicio, p - inicio); return p;
}

static const char* agrupar_letras(NodoLinea *linea, const char *p) {
    const char *inicio = p;
    while (*p != '\0' && *p != ' ') p++;
    agregar_palabra(linea, inicio, p - inicio); return p;
}

static void reconstruir_linea(NodoLinea *linea, const char *str) {
    liberar_palabras(linea);
    const char *p = str;
    while (*p != '\0') {
        if (*p == ' ') p = agrupar_espacios(linea, p);
        else p = agrupar_letras(linea, p);
    }
}

static void obtener_texto_linea(NodoLinea *linea, char *buffer) {
    buffer[0] = '\0';
    NodoPalabra *actual = linea->palabras_cabeza;
    size_t offset = 0;
    while (actual) {
        strcpy(buffer + offset, actual->texto);
        offset += actual->longitud;
        actual = actual->siguiente;
    }
}

void linea_insertar_caracter(NodoLinea *linea, size_t pos, char c) {
    if (pos > linea->longitud) pos = linea->longitud;
    char buffer[4096];
    obtener_texto_linea(linea, buffer);
    memmove(&buffer[pos + 1], &buffer[pos], linea->longitud - pos + 1);
    buffer[pos] = c;
    reconstruir_linea(linea, buffer);
}

void linea_eliminar_caracter(NodoLinea *linea, size_t pos) {
    if (pos >= linea->longitud) return;
    char buffer[4096];
    obtener_texto_linea(linea, buffer);
    memmove(&buffer[pos], &buffer[pos + 1], linea->longitud - pos);
    reconstruir_linea(linea, buffer);
}

int estructura_guardar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out) {
    int fd = open(nombre_archivo, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) return -1;
    
    size_t off = 0;
    size_t max_sz = 65000;
    SAFE_PRINT(log_out, off, max_sz, "==== INICIANDO GUARDADO A DISCO ====\r\n");
    SAFE_PRINT(log_out, off, max_sz, "1. Llamada: open(\"%s\", O_WRONLY | O_CREAT | O_TRUNC, 0644) => File Descriptor (fd): %d\r\n\r\n", nombre_archivo, fd);
    SAFE_PRINT(log_out, off, max_sz, "2. TRAZA DE CONSTRUCCION DEL BUFFER DE TEXTO (Linea por Linea, Palabra por Palabra):\r\n");

    char buffer[4096];
    int n_linea = 1;
    NodoLinea *actual = alm->cabeza;
    
    while (actual != NULL) {
        SAFE_PRINT(log_out, off, max_sz, "\r\n[Línea %d]:\r\n", n_linea);
        
        char buf_temp[4096] = "";
        NodoPalabra *pal = actual->palabras_cabeza;
        while(pal) {
            strncat(buf_temp, pal->texto, pal->longitud);
            SAFE_PRINT(log_out, off, max_sz, "   -> + Iterando NodoPalabra('%s') => Buffer RAM parcial: \"%s\"\r\n", pal->texto, buf_temp);
            pal = pal->siguiente;
        }
        
        obtener_texto_linea(actual, buffer);
        size_t len = strlen(buffer);
        buffer[len] = '\n';
        
        SAFE_PRINT(log_out, off, max_sz, "   -> FIN DE LÍNEA: Se anexa el salto de linea ('\\n') al buffer resultante.\r\n");
        SAFE_PRINT(log_out, off, max_sz, "   -> Llamada: write(fd: %d, buffer, %zu bytes) enviada al kernel.\r\n", fd, len + 1);
        
        // --- CALL SYSTEM: write() (Escritura en Disco) ---
        // El kernel toma los bytes de RAM y controla el hardware del disco
        // para persistir físicamente la información.
        write(fd, buffer, len + 1);
        n_linea++;
        actual = actual->siguiente;
    }
    
    close(fd);
    SAFE_PRINT(log_out, off, max_sz, "\r\n3. Llamada final: close(%d)\r\n", fd);
    SAFE_PRINT(log_out, off, max_sz, "\r\n==== GUARDADO COMPLETADO ====\r\n");
    
    return 0;
}

static void limpiar_estructura_para_cargar(EstructuraTexto *alm) {
    NodoLinea *actual = alm->cabeza;
    while (actual != NULL) {
        NodoLinea *siguiente = actual->siguiente;
        liberar_palabras(actual);
        free(actual);
        actual = siguiente;
    }
    alm->cabeza = NULL; alm->cola = NULL; alm->totalLineas = 0;
}

int estructura_cargar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out) {
    int fd = open(nombre_archivo, O_RDONLY);
    if (fd == -1) {
        if (log_out) sprintf(log_out, "open(\"%s\", O_RDONLY) => Falló (El archivo no existe)\r\n", nombre_archivo);
        return -1;
    }
    limpiar_estructura_para_cargar(alm);
    
    size_t off = 0;
    size_t max_sz = 65000;
    SAFE_PRINT(log_out, off, max_sz, "==== INICIANDO CARGA DE ARCHIVO A MEMORIA (ESTRUCTURA DE DATOS) ====\r\n");
    SAFE_PRINT(log_out, off, max_sz, "1. Llamada: open(\"%s\", O_RDONLY) => File Descriptor (fd): %d\r\n\r\n", nombre_archivo, fd);
    SAFE_PRINT(log_out, off, max_sz, "2. TRAZA DE CREACION DE NODOS Y TOKENIZACION (Línea por Línea, Palabra por Palabra):\r\n");

    NodoLinea *linea_cargando = estructura_insertar_linea(alm, NULL);
    char buffer_leido[4096];
    char linea_buf[4096];
    size_t len = 0;
    ssize_t bytes_leidos;
    int n_linea = 1;
    
    // --- CALL SYSTEM: read() (Lectura de Disco) ---
    // El kernel mueve los cabezales del disco (o consulta la celda de la SSD),
    // extrae los bytes y los deposita en el array 'buffer_leido' en RAM.
    while ((bytes_leidos = read(fd, buffer_leido, sizeof(buffer_leido))) > 0) {
        SAFE_PRINT(log_out, off, max_sz, "   [Llamada: read() retorno bloque de %zd bytes desde el disco]\r\n", bytes_leidos);
        
        for (ssize_t i = 0; i < bytes_leidos; i++) {
            char c = buffer_leido[i];
            if (c == '\n') {
                linea_buf[len] = '\0';
                SAFE_PRINT(log_out, off, max_sz, "\r\n-> [Línea %d detectada por('\\n')]: Extraida del disco: \"%s\"\r\n", n_linea, linea_buf);
                
                reconstruir_linea(linea_cargando, linea_buf);
                
                NodoPalabra *p = linea_cargando->palabras_cabeza;
                while(p) {
                    SAFE_PRINT(log_out, off, max_sz, "   -> + Se alojo dinámicamente un NodoPalabra('%s')\r\n", p->texto);
                    p = p->siguiente;
                }
                SAFE_PRINT(log_out, off, max_sz, "   -> La Línea %d completa fue enlazada en la memoria.\r\n", n_linea);
                
                linea_cargando = estructura_insertar_linea(alm, linea_cargando);
                len = 0;
                n_linea++;
            } else if (c != '\r') {
                if (len < 4095) linea_buf[len++] = c;
            }
        }
    }
    linea_buf[len] = '\0';
    if (len > 0) {
        SAFE_PRINT(log_out, off, max_sz, "\r\n-> [Línea %d (fin de archivo sin salto)]: \"%s\"\r\n", n_linea, linea_buf);
        reconstruir_linea(linea_cargando, linea_buf);
        NodoPalabra *p = linea_cargando->palabras_cabeza;
        while(p) {
            SAFE_PRINT(log_out, off, max_sz, "   -> + Se alojo dinámicamente un NodoPalabra('%s')\r\n", p->texto);
            p = p->siguiente;
        }
    }
    
    if (linea_cargando->longitud == 0 && alm->totalLineas > 1) {
        estructura_eliminar_linea(alm, linea_cargando);
    }
    
    close(fd);
    SAFE_PRINT(log_out, off, max_sz, "\r\n3. Llamada final: close(%d)\r\n", fd);
    SAFE_PRINT(log_out, off, max_sz, "\r\n==== CARGA COMPLETADA ====\r\n");

    return 0;
}

// ============================================================================
// EXTENSIONES PARA EL MODO DE COMANDOS CLI (o/p/a/d/i/s/q)
// ============================================================================

void linea_asignar_texto(NodoLinea *linea, const char *texto) {
    reconstruir_linea(linea, texto ? texto : "");
}

void estructura_obtener_texto_linea(NodoLinea *linea, char *buffer, size_t bufsize) {
    if (!buffer || bufsize == 0) return;
    // obtener_texto_linea interna asume un buffer suficientemente grande (4096);
    // usamos un intermedio propio y luego copiamos con límite seguro.
    char temp[4096];
    obtener_texto_linea(linea, temp);
    size_t len = strlen(temp);
    if (len >= bufsize) len = bufsize - 1;
    memcpy(buffer, temp, len);
    buffer[len] = '\0';
}

NodoLinea* estructura_obtener_linea(EstructuraTexto *alm, int n) {
    if (!alm || n < 1) return NULL;
    NodoLinea *actual = alm->cabeza;
    int i = 1;
    while (actual != NULL && i < n) { actual = actual->siguiente; i++; }
    return actual;
}

int estructura_buscar_todas(EstructuraTexto *alm, const char *palabra, int *lineas_encontradas, int max_resultados) {
    if (!alm || !palabra || strlen(palabra) == 0) return 0;
    NodoLinea *actual = alm->cabeza;
    int n_linea = 1, encontradas = 0;
    char buffer[4096];
    while (actual != NULL) {
        obtener_texto_linea(actual, buffer);
        if (strstr(buffer, palabra) != NULL) {
            if (encontradas < max_resultados && lineas_encontradas) {
                lineas_encontradas[encontradas] = n_linea;
            }
            encontradas++;
        }
        actual = actual->siguiente;
        n_linea++;
    }
    return encontradas;
}

int estructura_cargar_desde_fd(EstructuraTexto *alm, int fd) {
    // --- CALL SYSTEM: lseek() ---
    // Nos aseguramos de leer siempre desde el inicio del archivo,
    // independientemente de la posición previa del cursor del descriptor.
    if (lseek(fd, 0, SEEK_SET) == -1) return -1;

    limpiar_estructura_para_cargar(alm);

    NodoLinea *linea_cargando = estructura_insertar_linea(alm, NULL);
    char buffer_leido[4096];
    char linea_buf[4096];
    size_t len = 0;
    ssize_t bytes_leidos;

    // --- CALL SYSTEM: read() ---
    while ((bytes_leidos = read(fd, buffer_leido, sizeof(buffer_leido))) > 0) {
        for (ssize_t i = 0; i < bytes_leidos; i++) {
            char c = buffer_leido[i];
            if (c == '\n') {
                linea_buf[len] = '\0';
                reconstruir_linea(linea_cargando, linea_buf);
                linea_cargando = estructura_insertar_linea(alm, linea_cargando);
                len = 0;
            } else if (c != '\r') {
                if (len < 4095) linea_buf[len++] = c;
            }
        }
    }
    if (bytes_leidos == -1) return -1;

    linea_buf[len] = '\0';
    if (len > 0) reconstruir_linea(linea_cargando, linea_buf);

    /* A diferencia de estructura_cargar_archivo (usada por el editor visual,
       que siempre quiere al menos una línea en blanco para posicionar el
       cursor), en el modo de comandos un archivo de 0 bytes debe quedar
       representado como 0 líneas: así 'a' crea la línea 1 en vez de la 2. */
    if (linea_cargando->longitud == 0) {
        estructura_eliminar_linea(alm, linea_cargando);
    }
    return 0;
}

int estructura_sincronizar_reescribir(EstructuraTexto *alm, int fd) {
    // --- CALL SYSTEM: lseek() ---
    // Nos posicionamos explícitamente al inicio del archivo antes de escribir
    // el contenido completo actualizado.
    if (lseek(fd, 0, SEEK_SET) == -1) return -1;

    // Buffer dinámico (malloc/realloc) que crece según el tamaño real del
    // documento en memoria, evitando límites fijos y desplazamientos de
    // bytes manuales y propensos a error directamente sobre el disco.
    size_t capacidad = 4096;
    size_t total = 0;
    char *buffer_total = (char*)malloc(capacidad);
    if (!buffer_total) return -1;

    char linea_txt[4096];
    NodoLinea *actual = alm->cabeza;
    while (actual != NULL) {
        estructura_obtener_texto_linea(actual, linea_txt, sizeof(linea_txt));
        size_t len = strlen(linea_txt);
        // +1 por el '\n' que separa cada línea en disco
        while (total + len + 1 > capacidad) {
            capacidad *= 2;
            char *nuevo = (char*)realloc(buffer_total, capacidad);
            if (!nuevo) { free(buffer_total); return -1; }
            buffer_total = nuevo;
        }
        memcpy(buffer_total + total, linea_txt, len);
        total += len;
        buffer_total[total++] = '\n';
        actual = actual->siguiente;
    }

    // --- CALL SYSTEM: write() ---
    ssize_t escritos = write(fd, buffer_total, total);
    free(buffer_total);
    if (escritos == -1 || (size_t)escritos != total) return -1;

    // --- CALL SYSTEM: ftruncate() ---
    // Recorta el archivo al tamaño exacto del contenido nuevo. Esto es
    // fundamental cuando el documento se volvió más corto (comando 'd'):
    // sin ftruncate quedarían bytes "fantasma" de la versión anterior.
    if (ftruncate(fd, (off_t)total) == -1) return -1;

    return 0;
}

int estructura_sincronizar_append(int fd, const char *texto) {
    // --- CALL SYSTEM: lseek(SEEK_END) ---
    // Nos movemos al final del archivo sin necesidad de conocer su tamaño
    // exacto de antemano; el kernel lo calcula por nosotros.
    off_t pos = lseek(fd, 0, SEEK_END);
    if (pos == -1) return -1;

    size_t len = strlen(texto);
    char *linea_con_salto = (char*)malloc(len + 2);
    if (!linea_con_salto) return -1;
    memcpy(linea_con_salto, texto, len);
    linea_con_salto[len] = '\n';
    linea_con_salto[len + 1] = '\0';

    // --- CALL SYSTEM: write() ---
    ssize_t escritos = write(fd, linea_con_salto, len + 1);
    free(linea_con_salto);
    if (escritos == -1 || (size_t)escritos != len + 1) return -1;

    return 0;
}

int estructura_buscar_palabra(EstructuraTexto *alm, const char *palabra, NodoLinea **linea_encontrada, int *pos_x, int *pos_y) {
    if (!alm || !palabra || strlen(palabra) == 0) return 0;
    NodoLinea *actual = alm->cabeza; int y = 0; char buffer[4096];
    while (actual != NULL) {
        obtener_texto_linea(actual, buffer);
        char *coincidencia = strstr(buffer, palabra);
        if (coincidencia != NULL) {
            if (linea_encontrada) *linea_encontrada = actual;
            if (pos_x) *pos_x = (int)(coincidencia - buffer);
            if (pos_y) *pos_y = y; 
            return 1;
        }
        actual = actual->siguiente; y++;
    }
    return 0;
}
