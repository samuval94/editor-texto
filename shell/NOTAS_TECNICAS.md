# Notas Técnicas — Integración del Editor de Texto al Shell (SO2026B)

## 1. Punto de partida y problema detectado

El profesor entregó `editorEAFIT.zip` como base. Al revisarlo se encontró que
implementa un **editor visual de pantalla completa** (modo raw de terminal,
estilo `nano`), controlado con atajos de teclado (Ctrl+S, Ctrl+Q, Ctrl+F...).

La rúbrica, en cambio, exige un **ciclo interactivo de comandos de texto**:
`o [archivo]`, `p [n]`, `a [texto]`, `d [n]`, `i [n] [texto]`, `s [palabra]`,
`q` — es decir, una interfaz tipo `ed`, no una UI de pantalla completa.

## 2. Decisión de diseño

Se conservó el editor visual del profesor como modo opcional (`--visual`),
y se construyó **un nuevo módulo (`modo_comando.c`/`.h`)** que implementa el
ciclo o/p/a/d/i/s/q exigido por la rúbrica, **reutilizando** la estructura de
datos ya existente (`estructura.c`/`.h`): una lista enlazada dinámica de
líneas, cada una a su vez una lista enlazada dinámica de "palabras"
(`malloc`/`free`), que ya usaba syscalls de bajo nivel para cargar/guardar.

Este es el modo por defecto (`./editor archivo.txt`) y es el que se integró
al shell del curso.

## 3. Gestión del descriptor de archivo (fd)

Se mantiene **un único fd por sesión de edición**, abierto en `o` y cerrado
en `q` (o al abrir otro archivo con `o`, que primero cierra el anterior).
Esto es explícitamente lo que pide el Reto Técnico de nivel 1 ("gestión
básica de descriptores de archivo") y evita el patrón, menos elegante, de
abrir y cerrar el archivo en cada comando.

## 4. Mapeo de comandos a syscalls

| Comando | Syscalls usadas                              | Justificación |
|---------|-----------------------------------------------|----------------|
| `o`     | `open(O_RDWR\|O_CREAT, 0644)`                  | Abre o crea el archivo; guarda el fd de la sesión. |
| `p`     | `write()` a `STDOUT_FILENO` (fd 1)             | Imprime el contenido leído previamente a memoria. |
| `a`     | `lseek(SEEK_END)` + `write()`                  | Apéndice eficiente: no desplaza bytes existentes. |
| `d`     | `lseek(SEEK_SET)` + `write()` + `ftruncate()`  | Reescribe el archivo completo y recorta el sobrante (el archivo se acorta). |
| `i`     | `lseek(SEEK_SET)` + `write()` + `ftruncate()`  | Reescribe el archivo completo (los bytes se desplazan en memoria, no en disco). |
| `s`     | (solo memoria)                                 | Búsqueda de subcadena sobre el buffer ya cargado. |
| `q`     | `close()`                                      | Libera el fd y toda la memoria dinámica (`estructura_destruir`). |

**Por qué reescritura completa y no `lseek` byte a byte in-place para `d`/`i`:**
desplazar bytes arbitrarios directamente en disco (mover todo lo que sigue a
una posición N bytes antes/después) es frágil y propenso a errores cuando las
líneas tienen longitud variable. Por eso se mantiene el archivo completo en
un **buffer dinámico en memoria** (la lista enlazada, con `malloc`/`realloc`
para el buffer de reescritura), se modifica ahí, y solo entonces se
sincroniza a disco con `lseek`+`write`+`ftruncate`. Esto es exactamente lo
que pide la rúbrica de nivel "Parejas": *"manipulación de buffers dinámicos
en memoria (malloc/free) para no perder datos al desplazar bytes"*.

## 5. Integración con el Shell (`cat_edicion.c`)

Se creó una categoría nueva, `edicion`, con un único comando `editor
[archivo]`. Se creó como categoría nueva (y no reutilizando `datos`) porque
el resto del shell sigue el patrón "una syscall por comando, retorno
inmediato al prompt", mientras que el editor es un ciclo interactivo anidado
que toma el control de STDIN/STDOUT hasta que el usuario escribe `q`. Ver el
comentario extenso en `cat_edicion.c` para el detalle completo.

## 6. Verificación de memoria (Valgrind)

Se ejecutó `valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes`
tanto sobre el editor standalone como sobre el shell completo con el editor
integrado, cubriendo apertura, `a`, `i`, `d`, `s`, `p` y `q`. Resultado en
ambos casos:

```
FILE DESCRIPTORS: 3 open (3 std) at exit.
HEAP SUMMARY: in use at exit: 0 bytes in 0 blocks
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

## 7. Correcciones adicionales encontradas en el Makefile del shell

- El target `all` ejecutaba automáticamente el binario (`./$(ARCHSALIDA)`)
  tras compilar, bloqueando cualquier compilación no interactiva. Se separó
  en `all` (solo compila) y `run` (compila y ejecuta).
- El target `clean` usaba una variable `$(TARGET)` inexistente, por lo que
  nunca borraba el binario. Se corrigió a `$(ARCHSALIDA)`.

## 8. Pendientes para la entrega

- [ ] Grabar el video explicativo (5–7 min), cada estudiante explicando el
      módulo del otro (rotación de módulos ya prevista para la Semana 2).
- [ ] Redactar el informe técnico formal (PDF) — puede basarse directamente
      en estas notas.
- [ ] Revisión cruzada Samuel/Matías del código del otro antes del video.
