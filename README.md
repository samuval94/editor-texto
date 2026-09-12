# Editor de Texto EAFIT — Sistemas Operativos SO2026B

Editor de texto interactivo por línea de comandos, desarrollado como proyecto para el curso de Sistemas Operativos (SO2026B) de la Universidad EAFIT. Implementa un ciclo de edición tipo `ed` (abrir, imprimir, agregar, borrar, insertar, buscar, salir) usando exclusivamente llamadas al sistema POSIX de bajo nivel, e integra ese editor como un comando nuevo dentro del shell educativo desarrollado en el curso.

**Integrantes:** Samuel Valencia Montoya — Matías Zapata Rojas
**Curso:** Sistemas Operativos (SO2026B) — Universidad EAFIT

**Sistema operativo de desarrollo/prueba:** Linux (Fedora)

**Lenguaje:** C (`-std=c99` / `-std=gnu99`)

**Herramientas:** GCC, GNU Make, Valgrind, Bash, Git

---

## Contenido del repositorio

├── src/ # Editor de texto standalone (modo CLI + modo visual bonus)

├── Makefile # Compila el editor standalone

├── MANUAL_USUARIO.md

├── test_editor.sh

└── shell/ # Shell educativo del curso con el editor integrado

├── main.c, shell.h, cat_*.c

├── estructura.c/h, modo_comando.c/h, cat_edicion.c (motor del editor)

├── Makefile

└── NOTAS_TECNICAS.md


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

Se conserva además, como funcionalidad adicional, el editor visual de pantalla completa (estilo `nano`) entregado originalmente por el profesor como base, accesible con la bandera `--visual`.

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

### 1. Editor standalone

```bash
make clean && make
./editor demo.txt        # modo de comandos (por defecto)
```

Dentro del editor:
editor> a Hola mundo
editor> a Segunda línea
editor> p
editor> i 2 Línea insertada
editor> d 1
editor> s Línea
editor> q


También se puede correr la batería de pruebas automática:

```bash
./test_editor.sh
```

Y el modo visual (bonus, heredado de la base del profesor):

```bash
./editor --visual demo.txt
```

### 2. Shell con el editor integrado

```bash
cd shell
make clean && make
./eafitOS
```

Dentro del shell:

eafitOS> editor demo.txt
editor> a Probando desde el shell
editor> q
eafitOS> exit


### 3. Verificación de memoria (opcional pero recomendado)

```bash
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes ./editor demo.txt
```

---

## Documentación adicional

- `MANUAL_USUARIO.md` — manual de uso completo del editor.
- `shell/NOTAS_TECNICAS.md` — justificación de decisiones de arquitectura y mapeo de comandos a llamadas al sistema, usado como base para el informe técnico y la sustentación en video.