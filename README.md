# Compresor Huffman Concurrente integrado al Editor EAFIT — Sistemas Operativos SO2026B

Compresor y descompresor de archivos basado en **codificación de Huffman concurrente** (pool de hilos `pthread`, mutex y
variables de condición), integrado como **tarea en segundo plano** al editor de texto por línea de comandos del Parcial 1,
que a su vez vive dentro del shell educativo del curso (`eafitOS`). Proyecto del **Parcial 2 — Concurrencia y
Sincronización** (Alternativa 1: *Compresor de Archivos Huffman Concurrente Integrado al Editor*), Universidad EAFIT.

**Integrantes:** Samuel Valencia Montoya — Matías Zapata Rojas <br>
**Docente:** Edison Valencia Díaz <br>
**Curso:** Sistemas Operativos (SO2026B) — Universidad EAFIT <br>
**Sistema operativo de desarrollo/prueba:** Linux (Fedora) <br>
**Lenguaje:** C (`-std=gnu99`) con POSIX threads <br>
**Herramientas:** GCC, GNU Make, Valgrind, ThreadSanitizer/AddressSanitizer, Bash, Git <br>

---

## Qué hace

- **Comprime** el archivo abierto en el editor a un `.huf`, dividiéndolo en bloques de 256 KiB que procesa un pool de hilos.
- **Descomprime** un `.huf` verificando el CRC-32 de cada bloque; el resultado es idéntico al original (comprobado con `md5`).
- Corre **en segundo plano**: el editor sigue respondiendo, muestra progreso en tiempo real (`e` y avisos al 10 %, 20 %…)
  y permite cancelar (`c`) o esperar (`w`).
- Maneja la **condición de carrera** editor↔compresión: la tarea trabaja sobre un *snapshot* del archivo, así que se puede
  seguir editando sin corromper nada.

| Comando            | Acción                                                                  |
|--------------------|--------------------------------------------------------------------------|
| `z [salida]`       | Comprime el archivo abierto (por defecto `<archivo>.huf`).                |
| `x <huf> [salida]` | Descomprime verificando CRC-32 por bloque (no sobrescribe existentes).    |
| `e`                | Muestra barra y porcentaje de la tarea; además hay avisos cada 10 %.      |
| `c`                | Cancela la tarea en curso (no queda ningún archivo parcial).              |
| `w`                | Espera a que termine la tarea (útil en scripts).                          |

### Cómo está hecho (resumen)

| Pieza | Diseño |
|-------|--------|
| Conteo de frecuencias | Cada hilo cuenta en un histograma **local** (sin locks) y lo suma al global **una sola vez** bajo mutex. |
| Árbol de Huffman | El hilo coordinador lo construye con las frecuencias globales; códigos canónicos de máximo 15 bits. |
| Codificación | Los hilos codifican bloques en paralelo; cada bloque es independiente y lleva su CRC-32. |
| Escritura ordenada | El coordinador consume los bloques en orden 0, 1, 2… con una ventana acotada de slots (`mutex` + 2 variables de condición): sin espera activa y sin interbloqueos. |
| Resultado determinista | El `.huf` es idéntico con 1 o con 8 hilos. |
| Salida atómica | Se escribe a un temporal y se publica con `rename()`; si se cancela o falla, no queda nada. |
| Integración | Hilo de fondo + snapshot anónimo + reserva de rutas + una tarea a la vez + cierre ordenado con `join`. |

Detalle completo del protocolo de sincronización en [`shell/NOTAS_PARCIAL2.md`](shell/NOTAS_PARCIAL2.md).

---

## Contenido del repositorio

```text
.
├── README.md
├── MANUAL_USUARIO.md        # Manual de uso (editor + comandos de compresión)
└── shell/                   # Shell del curso, con el editor y el compresor integrados
    ├── huffman.c / .h       # Parcial 2: compresor Huffman concurrente (pool de hilos)
    ├── tarea_bg.c / .h      # Parcial 2: tarea en segundo plano del editor
    ├── huf_cli.c            # Parcial 2: utilidad 'huf' para probar el módulo sin el editor
    ├── test_huffman.sh      # Parcial 2: 36 pruebas (md5, carreras, TSan, ASan, Valgrind)
    ├── NOTAS_PARCIAL2.md    # Parcial 2: concurrencia y sincronización
    ├── modo_comando.c / .h  # Ciclo de comandos o/p/a/d/i/s/q + z/x/e/c/w del editor
    ├── cat_edicion.c        # Categoría 'edicion': registra el comando 'editor' en el shell
    ├── estructura.c / .h    # Buffer dinámico en memoria (líneas y palabras)
    ├── main.c, shell.h      # Shell del curso
    ├── cat_datos.c, cat_memoria.c, cat_monitoreo.c, cat_util.c
    ├── Makefile
    ├── NOTAS_TECNICAS.md    # Parcial 1: justificación de las decisiones de diseño
    ├── GUIA_DIDACTICA.md    # Guía del shell educativo (base entregada por el curso)
    ├── README.md            # Descripción del shell educativo base
    └── demo.txt             # Archivo de ejemplo para probar el editor
```

---

## Requisitos

- Linux (probado en Fedora; funciona igual en cualquier distro con GCC y Make)
- `gcc`, `make`
- (Opcional, para las pruebas completas) `valgrind`, `python3`

En Fedora, si falta algo:

```bash
sudo dnf install gcc make valgrind python3 git -y
```

---

## Cómo probarlo

Todo se hace parado dentro de la carpeta `shell/`:

```bash
cd shell
make            # compila el shell (eafitOS) y la utilidad 'huf'
./eafitOS
```

Dentro del shell (prompt `eafitOS>`) se abre el editor con un archivo y se comprime en segundo plano:

```text
eafitOS> editor informe.txt
editor> z                          # comprime en segundo plano -> informe.txt.huf
editor> e                          # barra y porcentaje de progreso
editor> a sigo editando mientras comprime
editor> w                          # espera a que termine
editor> x informe.txt.huf recuperado.txt
editor> w
editor> q
eafitOS> exit
```

Para ver el progreso con calma en un archivo pequeño, se puede añadir una pausa artificial por bloque:

```bash
EAFIT_HUF_RETARDO_MS=100 ./eafitOS
```

### Utilidad independiente

```bash
./huf c <entrada> <salida.huf> [hilos] [tam_bloque]
./huf d <entrada.huf> <salida> [hilos]
```

### Pruebas

```bash
make test                    # 36 pruebas completas (incluye sanitizers y Valgrind)
RAPIDO=1 ./test_huffman.sh   # omite sanitizers y Valgrind
```

Verifican: integridad por `md5` (vacío, 1 byte, ceros, aleatorio, texto, árbol profundo; 1/2/4/8 hilos; bloques de 1, 64 y
256 KiB), mismo `.huf` con 1 y 8 hilos, archivos corruptos o truncados, cancelación, ediciones durante la compresión,
reserva de rutas, regresión de los comandos del Parcial 1, **ThreadSanitizer** (0 carreras), **AddressSanitizer + UBSan +
LeakSanitizer** (0 errores) y **Valgrind**.

---

## Base: el editor de texto (Parcial 1)

El editor mantiene el archivo abierto **completo en memoria** (lista enlazada dinámica de líneas, cada una una lista de
palabras, con `malloc`/`free`) y sincroniza cada cambio a disco con llamadas de bajo nivel (`open`, `read`, `write`,
`lseek`, `ftruncate`, `close`), sin usar `fopen`/`fread`/`fwrite`/`fclose`.

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

## Documentación adicional

- [`MANUAL_USUARIO.md`](MANUAL_USUARIO.md) — manual de uso, incluidos los comandos de compresión.
- [`shell/NOTAS_PARCIAL2.md`](shell/NOTAS_PARCIAL2.md) — diseño concurrente del Parcial 2: pool de hilos, tubería ordenada, condiciones de carrera editor↔tarea y verificación.
- [`shell/NOTAS_TECNICAS.md`](shell/NOTAS_TECNICAS.md) — decisiones de arquitectura del editor y mapeo de comandos a llamadas al sistema (Parcial 1).
- [`shell/GUIA_DIDACTICA.md`](shell/GUIA_DIDACTICA.md) y [`shell/README.md`](shell/README.md) — documentación del shell educativo base del curso.
