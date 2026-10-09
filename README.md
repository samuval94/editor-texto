# Editor de Texto EAFIT — Sistemas Operativos SO2026B

Editor de texto interactivo por línea de comandos, desarrollado como proyecto para el curso de Sistemas Operativos (SO2026B) de la Universidad EAFIT. Implementa un ciclo de edición tipo `ed` (abrir, imprimir, agregar, borrar, insertar, buscar, salir) usando exclusivamente llamadas al sistema POSIX de bajo nivel, integrado como un comando nuevo (`editor`) dentro del shell educativo desarrollado en el curso.

**Integrantes:** Samuel Valencia Montoya — Matías Zapata Rojas <br>
**Curso:** Sistemas Operativos (SO2026B) — Universidad EAFIT <br>
**Sistema operativo de desarrollo/prueba:** Linux (Fedora) <br>
**Lenguaje:** C (`-std=c99` / `-std=gnu99`) <br>
**Herramientas:** GCC, GNU Make, Valgrind, Bash, Git <br>

---

## Contenido del repositorio

```text
.
├── README.md
├── MANUAL_USUARIO.md        # Manual de uso del editor (Parcial 1 + comandos del Parcial 2)
└── shell/                   # Shell del curso, con el editor integrado
    ├── main.c
    ├── shell.h
    ├── cat_datos.c
    ├── cat_memoria.c
    ├── cat_monitoreo.c
    ├── cat_util.c
    ├── cat_edicion.c        # Categoría nueva: registra el comando 'editor'
    ├── estructura.c / .h    # Buffer dinámico en memoria (líneas y palabras)
    ├── modo_comando.c / .h  # Ciclo de comandos o/p/a/d/i/s/q + z/x/e/c/w del editor
    ├── huffman.c / .h       # Parcial 2: compresor Huffman concurrente (pool de hilos)
    ├── tarea_bg.c / .h      # Parcial 2: tarea en segundo plano del editor
    ├── huf_cli.c            # Parcial 2: utilidad 'huf' para probar el módulo
    ├── Makefile
    ├── test_huffman.sh      # Parcial 2: 36 pruebas (md5, carreras, TSan, ASan, Valgrind)
    ├── NOTAS_TECNICAS.md    # Justificación de las decisiones de diseño (Parcial 1)
    ├── NOTAS_PARCIAL2.md    # Concurrencia y sincronización (Parcial 2)
    ├── GUIA_DIDACTICA.md    # Guía del shell educativo (base entregada por el curso)
    ├── README.md            # Descripción del shell educativo base
    └── demo.txt             # Archivo de ejemplo para probar el editor
```


---

## ¿Qué hace el proyecto?

El editor mantiene el archivo abierto **completo en memoria** (una lista enlazada dinámica de líneas, cada línea a su vez una lista de palabras, usando `malloc`/`free`), y sincroniza cada cambio de vuelta al disco con llamadas de bajo nivel (`open`, `read`, `write`, `lseek`, `ftruncate`, `close`) — nunca con `fopen`/`fread`/`fwrite`/`fclose`.

| Comando          | Acción                                                          |
|------------------|------------------------------------------------------------------|
| `o <archivo>`    | Abre el archivo (o lo crea si no existe).                        |
| `p [n]`          | Imprime la línea `n`, o todo el archivo si se omite `n`.          |
| `a <texto>`      | Agrega `<texto>` como nueva línea al final.                      |
| `d <n>`          | Borra la línea `n`.                                              |
| `i <n> <texto>`  | Inserta `<texto>` como la línea `n`, desplazando el resto.        |
| `s <palabra>`    | Busca `<palabra>` e indica en qué líneas aparece.                 |
| `q`              | Cierra el archivo (libera el descriptor) y termina.               |


---

## Requisitos

- Linux (probado en Fedora; funciona igual en cualquier distro con GCC y Make)
- `gcc`, `make`
- (Opcional, para verificación de memoria) `valgrind`

En Fedora, si falta algo:

```bash
sudo dnf install gcc make valgrind git -y
```

---

## Cómo probarlo

Todo se hace parado dentro de la carpeta `shell/`:

```bash
cd shell
make
./eafitOS
```

Ya dentro del shell (prompt `eafitOS>`), se invoca el editor con:

eafitOS > editor demo.txt


Eso abre (o crea) `demo.txt` y entra al modo de comandos del editor (prompt `editor>`):
```
editor> a Hola mundo 
editor> a Segunda línea
editor> p
editor> i 2 Línea insertada
editor> d 1
editor> s Línea
editor> q
```
Al escribir 'q', el control regresa al shell (prompt `eafitOS>` de nuevo). Para salir del shell completamente:

```
eafitOS> exit
```

## Parcial 2: Compresión Huffman concurrente en segundo plano

El editor suma cinco comandos que **no bloquean** la interfaz (el trabajo corre en un pool de hilos `pthread`):

| Comando            | Acción                                                                 |
|--------------------|-------------------------------------------------------------------------|
| `z [salida]`       | Comprime el archivo abierto (por defecto `<archivo>.huf`).               |
| `x <huf> [salida]` | Descomprime verificando CRC-32 por bloque (no sobrescribe existentes).   |
| `e`                | Muestra barra y porcentaje de la tarea; además hay avisos cada 10 %.     |
| `c`                | Cancela la tarea en curso.                                               |
| `w`                | Espera a que termine la tarea (útil en scripts).                         |

Mientras se comprime se puede seguir editando (`a`, `d`, `i`): la tarea trabaja sobre un *snapshot* del archivo. Detalles del protocolo de sincronización en `shell/NOTAS_PARCIAL2.md`.

```bash
cd shell
make            # compila el shell (eafitOS) y la utilidad 'huf'
make test       # 36 pruebas (RAPIDO=1 ./test_huffman.sh omite sanitizers y valgrind)
```

## Documentación adicional

- `MANUAL_USUARIO.md` — manual de uso del editor, incluidos los comandos de compresión.
- `shell/NOTAS_PARCIAL2.md` — diseño concurrente del Parcial 2: pool de hilos, tubería ordenada, condiciones de carrera editor↔tarea y verificación.
- `shell/GUIA_DIDACTICA.md` y `shell/README.md` — documentación del shell educativo base del curso.
- `shell/NOTAS_TECNICAS.md` — justificación de decisiones de arquitectura y mapeo de comandos a llamadas al sistema, usado como base para el informe técnico y la sustentación en video.