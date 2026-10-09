# Manual de usuario — Editor de Texto EAFIT

Editor de texto por línea de comandos (estilo `ed`) integrado al shell educativo `eafitOS`, con compresión
Huffman concurrente en segundo plano.

## 1. Compilar y arrancar

```bash
cd shell
make            # compila el shell (eafitOS) y la utilidad 'huf'
./eafitOS       # prompt: eafitOS>
```

Dentro del shell:

```text
eafitOS> editor                # abre el editor sin archivo
eafitOS> editor demo.txt       # abre (o crea) demo.txt
```

El prompt del editor es `editor>`. Con `q` se cierra el archivo y se regresa al shell; con `exit` se sale del shell.
`h` o `?` muestra la ayuda de comandos.

## 2. Comandos de edición

| Comando          | Acción                                                                  |
|------------------|-------------------------------------------------------------------------|
| `o <archivo>`    | Abre el archivo (lo crea si no existe). Cierra el anterior si lo había.  |
| `p [n]`          | Imprime la línea `n`, o todo el archivo (numerado) si se omite `n`.      |
| `a <texto>`      | Añade `<texto>` como nueva línea al final.                               |
| `d <n>`          | Borra la línea `n`.                                                      |
| `i <n> <texto>`  | Inserta `<texto>` como línea `n`, desplazando el resto.                  |
| `s <palabra>`    | Indica en qué líneas aparece `<palabra>`.                                |
| `q`              | Cierra el archivo y sale del editor.                                     |

Cada cambio (`a`, `d`, `i`) se escribe en disco de inmediato: no existe un comando "guardar" aparte.

Ejemplo:

```text
editor> o demo.txt
editor> a Hola mundo
editor> a Segunda línea
editor> i 2 Línea insertada
editor> p
editor> s Línea
editor> q
```

## 3. Compresión Huffman en segundo plano

Estos comandos **no bloquean** el editor: la tarea corre en hilos propios y se puede seguir editando.

| Comando              | Acción                                                                      |
|----------------------|-----------------------------------------------------------------------------|
| `z [salida]`         | Comprime el archivo abierto. Por defecto escribe `<archivo>.huf`.            |
| `x <huf> [salida]`   | Descomprime un `.huf` verificando el CRC-32 de cada bloque. Por defecto quita `.huf`. |
| `e`                  | Muestra barra y porcentaje de la tarea en curso, con su fase.                |
| `c`                  | Cancela la tarea en curso (no queda ningún archivo parcial).                 |
| `w`                  | Espera a que termine la tarea (útil en scripts). `Ctrl+C` solo abandona la espera. |

Además, el editor imprime avisos solos al 10 %, 20 % … 90 % y al terminar.

Ejemplo:

```text
editor> o informe.txt
editor> z
Compresión Huffman iniciada en segundo plano: 'informe.txt' -> 'informe.txt.huf'.
editor> a sigo editando mientras comprime
editor> e
[compresión en curso] 'informe.txt' -> 'informe.txt.huf'
  [##########..........] 50%  (fase: codificación de bloques, 0.4 s)
[bg] Compresión terminada: 'informe.txt' -> 'informe.txt.huf' (... bytes, ...% del original, ... s)
[bg] Aviso: el documento se editó 1 vez/veces durante la compresión; el .huf contiene la versión del momento de iniciar (snapshot).
editor> x informe.txt.huf informe_recuperado.txt
```

### Reglas que conviene conocer

- **El `.huf` es una foto del archivo al ejecutar `z`.** Lo que edite después no entra en ese `.huf`; el editor se lo avisa al terminar.
- **Una sola tarea a la vez.** Mientras haya una en curso, otro `z`/`x` se rechaza (use `e`, `c` o `w`).
- **Rutas reservadas.** Mientras la tarea vive, no se puede abrir con `o` su archivo de salida (ni el `.huf` que se está leyendo), ni usarlo como salida de otra tarea. Se detectan también los alias (`./x.huf` es lo mismo que `x.huf`).
- **Nunca se sobrescribe.** `z` y `x` se niegan si la salida ya existe; indique otro nombre.
- **La salida no puede ser el archivo abierto** en el editor.
- **Al salir** (`q`, `Ctrl+D`, `Ctrl+C` o `SIGTERM`) con una tarea activa, se cancela, se espera a los hilos y se libera todo antes de cerrar.
- **Atomicidad.** La salida se escribe a un temporal y solo se publica (`rename`) si todo salió bien; si se cancela o falla, no queda nada.

## 4. Utilidad `huf` (sin el editor)

```bash
./huf c <entrada> <salida.huf> [hilos] [tam_bloque]
./huf d <entrada.huf> <salida> [hilos]
```

Sirve para probar el compresor de forma independiente, medir tiempos con distinto número de hilos y ejecutar los
sanitizers/Valgrind. Si se omiten, los hilos se eligen según las CPUs y el bloque es de 256 KiB.

## 5. Pruebas

```bash
cd shell
make test                    # 36 pruebas completas (puede tardar por sanitizers y Valgrind)
RAPIDO=1 ./test_huffman.sh   # omite sanitizers y Valgrind
```

Requisitos opcionales: `valgrind`, `python3` (genera los datos de prueba).

Para ver la cancelación y el progreso con calma se puede ralentizar artificialmente cada bloque:

```bash
EAFIT_HUF_RETARDO_MS=50 ./eafitOS
```

## 6. Mensajes de error frecuentes

| Mensaje                                              | Causa / solución                                                    |
|------------------------------------------------------|---------------------------------------------------------------------|
| `No hay ningún archivo abierto. Use: o <archivo>`    | Abra un archivo con `o` antes de editar o comprimir.                 |
| `Ya hay una tarea en segundo plano en curso`         | Espere (`w`), consulte (`e`) o cancele (`c`).                        |
| `'<ruta>' ya existe; no se sobrescribe`              | Indique otra salida: `z otro.huf` o `x f.huf otro.txt`.              |
| `'<ruta>' está en uso por la tarea en segundo plano` | La ruta está reservada; espere o cancele la tarea.                   |
| `CRC-32 incorrecto en el bloque N`                   | El `.huf` está dañado; no se genera ninguna salida.                  |
| `no es un archivo .huf válido`                       | El archivo no tiene la firma `HUF1`.                                 |

## 7. Limitaciones conocidas

- Los códigos de Huffman están limitados a 15 bits (subóptimo solo en distribuciones extremas).
- El snapshot copia temporalmente el archivo a `/tmp`: necesita espacio libre igual al tamaño del archivo, y `z` tarda
  en devolver el prompt proporcionalmente a ese tamaño.
- Los avisos asíncronos se imprimen sobre la línea de entrada; lo que se esté escribiendo en ese momento sigue en el
  búfer del terminal aunque no se vea.

Diseño detallado de la concurrencia: `shell/NOTAS_PARCIAL2.md`.
