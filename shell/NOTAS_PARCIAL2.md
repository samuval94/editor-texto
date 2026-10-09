# Notas Técnicas — Parcial 2 (Alternativa 1): Compresor Huffman Concurrente integrado al Editor

## 1. Qué se agregó

| Archivo            | Rol |
|--------------------|-----|
| `huffman.c/.h`     | Motor de compresión/descompresión: pool de hilos, conteo paralelo de frecuencias, codificación por bloques, escritura ordenada, CRC-32. No sabe nada del editor. |
| `tarea_bg.c/.h`    | *Background worker* del editor: hilo de fondo, progreso, cancelación, snapshot, reserva de rutas, cierre limpio. |
| `modo_comando.c`   | Nuevos comandos `z`, `x`, `e`, `c`, `w`; manejo de SIGINT/SIGTERM; ganchos de sincronización en `o`, `a`, `d`, `i`, `q`. |
| `huf_cli.c`        | Utilidad `huf c|d` para probar el módulo sin el editor (benchmarks, TSan, Valgrind). |
| `test_huffman.sh`  | 36 pruebas automáticas (integridad md5, orden, corrupción, carreras, cancelación, sanitizers, Valgrind). |

Comandos nuevos del editor (ninguno bloquea el prompt):

| Comando | Acción |
|---------|--------|
| `z [salida]` | Comprime el archivo abierto (por defecto `<archivo>.huf`). |
| `x <huf> [salida]` | Descomprime verificando CRC-32 por bloque (por defecto quita `.huf`). No sobrescribe archivos existentes. |
| `e` | Barra y porcentaje de la tarea en curso, con su fase. Además se imprimen avisos asíncronos al 10 %, 20 %… 90 % y al terminar. |
| `c` | Cancela la tarea en curso. |
| `w` | Espera a que termine (útil en scripts). `Ctrl+C` durante `w` solo abandona la espera. |

## 2. Algoritmo y formato

**Compresión en dos fases, ambas paralelas**

1. *Conteo de frecuencias.* Los N hilos del pool toman bloques (256 KiB por defecto) con `pread()`, cuentan en un **histograma local** (sin locks) y, al terminar, suman su histograma al global bajo mutex (**reducción local**: una sola sección crítica por hilo).
2. El hilo coordinador construye el árbol de Huffman global y los **códigos canónicos** (longitud máxima 15 bits; si el árbol resulta más profundo se atenúan las frecuencias y se reconstruye).
3. *Codificación.* Los hilos codifican bloques en paralelo; el coordinador escribe los bloques **en orden secuencial** (§3).

**Descompresión:** cada bloque es independiente (empieza en un byte), así que se decodifica en paralelo con tabla de búsqueda de 15 bits, se verifica su CRC-32 y se escribe en orden con la misma tubería.

**Formato `.huf`** (little-endian): `"HUF1"`, `u32` tam. bloque, `u64` tam. original, `u32` nº bloques, `u8[256]` longitudes de código, tabla `{u32 tam_comprimido, u32 crc32}` por bloque, y los payloads. Como el tamaño de cada bloque comprimido se conoce solo al final, la cabecera se escribe al terminar con `pwrite()`.

**Escritura atómica:** la salida va a un temporal (`mkstemp`, mismo directorio) y se publica con `rename()` solo si todo salió bien; en error o cancelación se borra con `unlink()`. Nunca queda un `.huf` a medias.

**Resultado determinista:** el `.huf` es idéntico con 1 o con 8 hilos (prueba 2 del script), evidencia de que el ensamblado ordenado es correcto.

## 3. Sincronización (rúbrica: mecanismos de sincronización, 25 pts)

### 3.1 Pool de hilos (`Pool`)
Hilos persistentes durante toda la operación. `pool_iniciar()` publica una *generación* (función + argumento) y hace `broadcast` en `cv_start`; `pool_esperar()` duerme en `cv_fin` hasta que el último trabajador termina. Sin `sleep`, sin sondeo.

### 3.2 Tubería productor–consumidor con ventana acotada (`Job`)
Un único mutex `mu` protege `next_job`, `next_write`, `slots[]`, `abort`, `rc`, `msg` y `freq[]`.

- **Trabajadores (productores):** toman `idx = next_job++` solo si `next_job < next_write + W` (ventana de W bloques en vuelo); si no, esperan en `cv_space`. Procesan **fuera** del mutex y depositan el resultado en `slots[idx % W]`.
- **Coordinador (consumidor):** espera en `cv_slot` hasta que el slot `i` esté listo, lo toma, escribe a disco **fuera** del mutex, **vacía el slot y luego** incrementa `next_write` y hace `broadcast(cv_space)`.
- **Orden garantizado:** el coordinador consume estrictamente 0, 1, 2… independientemente de qué hilo termine primero.

**Por qué no hay interbloqueo:** el bloque `next_write` siempre está dentro de la ventana, por lo que algún trabajador puede producirlo; el coordinador espera solo ese bloque, y los trabajadores esperan solo si el coordinador está ocupado escribiendo (y despierta a todos al avanzar).
**Por qué no hay choque de slots:** `idx < next_write + W` ⇒ el ocupante anterior (`idx − W`) ya fue consumido y el slot vaciado.
**Memoria acotada:** como máximo W bloques (por defecto 4 × hilos) están en memoria, sin importar el tamaño del archivo.

### 3.3 Abortar / cancelar
`job_abortar()` (bajo `mu`) marca `abort`, guarda el primer error y hace `broadcast` en ambas variables de condición, así que nadie se queda dormido. La cancelación del usuario es un campo **atómico** (`__atomic_store_n`) que los trabajadores consultan entre bloques; no requiere señalar ninguna condición porque siempre hay un trabajador activo que lo ve y llama a `job_abortar()`.

### 3.4 Datos por bloque (`clen[]`, `crc[]`)
Los escribe un único trabajador antes de publicar el slot bajo `mu`; el coordinador los lee después de tomar `mu` (relación *happens-before* por el mutex) y el hilo de la tarea los lee tras `pool_esperar()`.

### 3.5 Progreso
Contadores atómicos (`hecho`, `fase`, `cancelar`). Los avisos al 10 %, 20 %… los dispara **el propio trabajador que cruza el umbral** mediante un *callback* (sin hilo "reportero" ni sondeo). El callback toma el mutex de la tarea para no repetir hitos y emite el mensaje con un único `write()`.

### 3.6 Sin espera activa
Toda espera es `pthread_cond_wait` o `pthread_cond_timedwait`. El único `timedwait` (200 ms) está en el comando `w`, solo para poder atender `Ctrl+C`; el hilo duerme entre intervalos, no gira.

## 4. Integración con el editor (rúbrica: 20 pts) — manejo de condiciones de carrera

El hilo de la interfaz es **el único que modifica** el documento y el archivo. Se aplican cuatro reglas:

1. **Snapshot (aislamiento por copia).** Al ejecutar `z`, la UI copia el archivo abierto —byte a byte, tal como está en disco, usando `pread`— a un archivo temporal **anónimo** (`mkstemp` + `unlink` inmediato) y la tarea comprime *esa copia*. La tarea nunca lee el archivo del usuario mientras `a`/`d`/`i` hacen `lseek`+`write`+`ftruncate`, así que no existen lecturas rotas. El `.huf` contiene exactamente la versión del momento de `z`; el editor cuenta las ediciones posteriores y lo avisa al terminar.
2. **Reserva de rutas.** Mientras la tarea vive, su archivo de salida (y el `.huf` que se está leyendo, en descompresión) queda reservado: `o` sobre esa ruta, otro `z`/`x` hacia ella, o apuntar la salida al archivo abierto se rechazan (comparación por nombre, **por ruta canónica** —`realpath()` del directorio + nombre base, así `./x.huf` y `x.huf` coinciden aunque el destino aún no exista— **y por (dev, inode)**).
3. **Una sola tarea a la vez**, con el estado (`LIBRE/CORRIENDO/TERMINADA`) protegido por mutex.
4. **Cierre ordenado.** `q`, EOF y las señales cancelan la tarea, hacen `pthread_join` y liberan todo antes de salir.

> Nota de diseño: "guardar" en este editor es inmediato (cada `a`/`d`/`i` sincroniza el archivo). Por eso la condición de carrera editar/guardar-durante-compresión se resuelve con el snapshot, sin bloquear al usuario ni obligarlo a esperar.

## 5. Errores y recursos (rúbrica: 15 pts)

- Todo retorno de syscall se verifica (`open`, `pread`, `pwrite`, `write`, `fstat`, `mkstemp`, `fsync`, `rename`, `pthread_create`…); los mensajes usan `strerror_r` (seguro entre hilos).
- Lectura/escritura completas con reintento en `EINTR` y manejo de lecturas cortas; EOF inesperado (archivo cambiado) se detecta y aborta.
- Cabecera validada al descomprimir: firma, tamaños, `n_bloques` coherente, desigualdad de Kraft, suma de tamaños = tamaño del archivo; CRC-32 por bloque.
- `pthread_join` de todos los hilos (pool y tarea); sin `detach`. Hilos creados con **todas las señales bloqueadas**; `SIGINT`/`SIGTERM` los atiende solo el hilo de la UI (`sigaction` sin `SA_RESTART`, para que `fgets` retorne y se limpie), y se restauran los manejadores originales al salir (el shell sigue vivo).
- Si falla `pthread_create` a mitad del pool, se apagan y unen los hilos ya creados.

## 6. Verificación

`make test` (o `./test_huffman.sh`) ejecuta:

- Integridad por `md5` con vacío, 1 byte, ceros, aleatorio, texto y distribución tipo Fibonacci (árbol profundo), para 1/2/4/8 hilos y bloques de 1 KiB, 64 KiB y 256 KiB.
- Mismo `.huf` con 1 y con 8 hilos.
- Corrupción, truncado y firma falsa: error limpio, sin salida parcial ni temporales.
- Editor: ciclo `z`/`x`, ediciones durante la compresión (snapshot), progreso, cancelación, `q` con tarea activa, reserva de rutas, no sobrescribir, regresión de los comandos del Parcial 1.
- **ThreadSanitizer** (0 carreras), **AddressSanitizer + UBSan + LeakSanitizer** (0 errores/fugas) y **Valgrind** (0 errores, todos los bloques liberados).

Para demos y pruebas de cancelación existe la variable `EAFIT_HUF_RETARDO_MS=<ms>` que añade una pausa artificial por bloque (con valor 0/ausente no hace nada).

## 7. Limitaciones conocidas (para la sustentación)

- Los códigos están limitados a 15 bits (atenuación de frecuencias): subóptimo solo en distribuciones extremas.
- El snapshot duplica temporalmente el archivo en `/tmp` (espacio en disco = tamaño del archivo).
- Los avisos de progreso se imprimen sobre la línea de entrada; en terminal se borra la línea y se repinta el prompt, pero lo que el usuario esté tecleando queda en el buffer del terminal sin eco.
- `z` y `x` no sobrescriben archivos existentes (decisión de seguridad); hay que indicar otra salida. La comprobación `access()` y la publicación con `rename()` no son atómicas entre sí (ventana TOCTOU mínima, solo relevante si otro proceso externo crea el archivo en ese instante).
- El snapshot lo copia el hilo de la UI: `z` tarda en devolver el prompt proporcionalmente al tamaño del archivo (≈0.13 s para 300 MB con el archivo en caché de página; más en discos lentos). Es el precio de que la tarea y el editor no compartan ningún dato.
- El comando `w` usa `pthread_cond_timedwait` de 200 ms únicamente para poder atender Ctrl+C; el hilo duerme entre intervalos (no es espera activa).
