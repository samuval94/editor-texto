#!/usr/bin/env bash
# ============================================================================
# test_huffman.sh - Pruebas del Parcial 2 (Alternativa 1): Huffman concurrente
# integrado al editor. Imprime PASS/FAIL por prueba y devuelve 0 solo si todo pasa.
#
# Uso:   ./test_huffman.sh            (completo, incluye sanitizers y valgrind)
#        RAPIDO=1 ./test_huffman.sh   (omite sanitizers y valgrind)
# ============================================================================
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR" || exit 1
make -s all || { echo "FAIL: no compila"; exit 1; }
SHELL_BIN="$DIR/eafitOS"
HUF="$DIR/huf"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
OK=0; FALLOS=0
pass() { echo "  PASS  $1"; OK=$((OK+1)); }
fail() { echo "  FAIL  $1"; FALLOS=$((FALLOS+1)); }
check() { # check "descripcion" comando...
    local d="$1"; shift
    if "$@" >/dev/null 2>&1; then pass "$d"; else fail "$d"; fi
}
limpio() { sed 's/\x1b\[[0-9;]*[mK]//g'; }
# Ejecuta el shell con el editor; $1 = guion de comandos del editor (sin 'editor' ni 'exit')
editor() { ( cd "$T" && printf 'editor %s\n%s\nexit\n' "$1" "$2" | timeout 120 "$SHELL_BIN" 2>&1 | limpio ); }

# ---------------------------------------------------------------- datos de prueba
cd "$T" || exit 1
: > vacio.bin
printf 'a' > uno.bin
head -c 1000000 /dev/zero > ceros.bin
head -c 2000000 /dev/urandom > azar.bin
python3 - <<'PY'
import random
random.seed(7)
palabras = open('/usr/include/stdio.h').read().split() or ['hola', 'mundo']
open('texto.txt', 'w').write('\n'.join(' '.join(random.choice(palabras) for _ in range(12)) for _ in range(120000)) + '\n')
# Distribución tipo Fibonacci: árbol de Huffman muy profundo (prueba el límite de 15 bits)
b = bytearray(); a, c = 1, 1
for i in range(30):
    b += bytes([i]) * a; a, c = c, a + c
random.shuffle(b); open('fib.bin', 'wb').write(b)
PY
cd "$DIR" || exit 1

echo "== 1. Integridad (compresión -> descompresión idéntica, md5) =="
for f in vacio.bin uno.bin ceros.bin azar.bin texto.txt fib.bin; do
  malo=0
  for h in 1 2 4 8; do for bs in 1024 65536 262144; do
    "$HUF" c "$T/$f" "$T/$f.huf" "$h" "$bs" >/dev/null 2>&1 || malo=1
    "$HUF" d "$T/$f.huf" "$T/$f.out" "$h" >/dev/null 2>&1 || malo=1
    [ "$(md5sum < "$T/$f")" = "$(md5sum < "$T/$f.out" 2>/dev/null)" ] || malo=1
    rm -f "$T/$f.out"
  done; done
  [ $malo -eq 0 ] && pass "$f: 4 configuraciones de hilos x 3 tamaños de bloque" || fail "$f"
done

echo "== 2. Ensamblado ordenado: la salida NO depende del nº de hilos =="
"$HUF" c "$T/texto.txt" "$T/h1.huf" 1 65536 >/dev/null; "$HUF" c "$T/texto.txt" "$T/h8.huf" 8 65536 >/dev/null
check "mismo .huf con 1 y con 8 hilos (bit a bit)" cmp "$T/h1.huf" "$T/h8.huf"
tam_o=$(stat -c%s "$T/texto.txt"); tam_c=$(stat -c%s "$T/h8.huf")
[ "$tam_c" -lt "$tam_o" ] && pass "el texto realmente se comprime ($tam_o -> $tam_c bytes)" || fail "no comprime el texto"

echo "== 3. Archivos dañados: error limpio y sin salida parcial =="
cp "$T/h8.huf" "$T/mal.huf"; printf '\xff\xff\xff' | dd of="$T/mal.huf" bs=1 seek=50000 conv=notrunc 2>/dev/null
"$HUF" d "$T/mal.huf" "$T/mal.out" >/dev/null 2>&1; rc=$?
[ $rc -ne 0 ] && [ ! -e "$T/mal.out" ] && pass "byte corrupto detectado (CRC/decodificación), sin archivo de salida" || fail "corrupción no detectada"
head -c 3000 "$T/h8.huf" > "$T/trunc.huf"
"$HUF" d "$T/trunc.huf" "$T/trunc.out" >/dev/null 2>&1; rc=$?
[ $rc -ne 0 ] && [ ! -e "$T/trunc.out" ] && pass "archivo truncado rechazado" || fail "truncado aceptado"
echo "no soy un huf" > "$T/falso.huf"
"$HUF" d "$T/falso.huf" "$T/falso.out" >/dev/null 2>&1; rc=$?
[ $rc -ne 0 ] && pass "firma inválida rechazada" || fail "firma inválida aceptada"
n=$(ls "$T" | grep -c '\.[A-Za-z0-9]\{6\}$'); [ "$n" -eq 0 ] && pass "no quedan temporales huérfanos (mkstemp)" || fail "temporales huérfanos: $n"

echo "== 4. Integración con el editor (segundo plano) =="
cp "$T/texto.txt" "$T/doc.txt"; MD_ORIG=$(md5sum < "$T/doc.txt")

SAL=$(editor doc.txt $'z\nw\nx doc.txt.huf doc.rec\nw\nq')
echo "$SAL" | grep -q "Compresión terminada" && echo "$SAL" | grep -q "CRC-32 verificado" && pass "z + w + x: ciclo completo en segundo plano" || fail "ciclo z/x"
[ "$(md5sum < "$T/doc.rec" 2>/dev/null)" = "$MD_ORIG" ] && pass "descomprimido idéntico al original (md5)" || fail "md5 distinto tras el ciclo del editor"
rm -f "$T/doc.rec" "$T/doc.txt.huf"

# Carrera edición/compresión: ediciones durante la tarea no corrompen ni se mezclan (snapshot)
SAL=$(EAFIT_HUF_RETARDO_MS=20 editor doc.txt $'z\na LINEA_EXTRA_DURANTE_COMPRESION\nd 1\nw\nx doc.txt.huf doc.snap\nw\nq')
[ "$(md5sum < "$T/doc.snap" 2>/dev/null)" = "$MD_ORIG" ] && pass "edición concurrente: el .huf conserva la versión del snapshot" || fail "snapshot inconsistente"
[ "$(md5sum < "$T/doc.txt")" != "$MD_ORIG" ] && pass "las ediciones sí se aplicaron al archivo del usuario (UI no bloqueada)" || fail "las ediciones no se aplicaron"
echo "$SAL" | grep -q "se editó 2 vez" && pass "el editor avisa de las ediciones ocurridas durante la tarea" || fail "sin aviso de ediciones"
cp "$T/texto.txt" "$T/doc.txt"; rm -f "$T/doc.snap" "$T/doc.txt.huf"

# Progreso en tiempo real
SAL=$(EAFIT_HUF_RETARDO_MS=20 editor doc.txt $'z\ne\nw\nq')
echo "$SAL" | grep -q "compresión en curso" && echo "$SAL" | grep -qE "\[#*\.*\] *[0-9]+%" && pass "comando 'e': barra y porcentaje" || fail "comando e"
echo "$SAL" | grep -q "Comprimiendo: 50%" && pass "avisos de progreso asíncronos (10%..90%)" || fail "sin avisos de progreso"
rm -f "$T"/doc.txt.huf*

# Cancelación
SAL=$(EAFIT_HUF_RETARDO_MS=40 editor doc.txt $'z\nc\nw\nq')
echo "$SAL" | grep -q "Tarea cancelada" && [ -z "$(ls "$T" | grep 'doc.txt.huf')" ] && pass "cancelar ('c'): sin salida ni temporales" || fail "cancelación"

# q con tarea en curso
SAL=$(EAFIT_HUF_RETARDO_MS=40 editor doc.txt $'z\nq')
echo "$SAL" | grep -q "Cancelando la tarea" && echo "$SAL" | grep -q "Editor cerrado" && [ -z "$(ls "$T" | grep 'doc.txt.huf')" ] && pass "'q' durante la tarea: cancela, hace join y cierra limpio" || fail "q con tarea"
[ -z "$(ls /tmp | grep eafit_snap)" ] && pass "el snapshot anónimo no deja rastro en /tmp" || fail "snapshot residual en /tmp"

# Reserva de rutas / una tarea a la vez
SAL=$(EAFIT_HUF_RETARDO_MS=40 editor doc.txt $'z\nz otra.huf\nz\no doc.txt.huf\nc\nw\nq')
echo "$SAL" | grep -q "Ya hay una tarea" && pass "solo una tarea a la vez" || fail "dos tareas simultáneas"
echo "$SAL" | grep -q "reservado por otra tarea" && pass "la misma salida no se puede reservar dos veces" || fail "reserva duplicada"
echo "$SAL" | grep -q "en uso por la tarea" && pass "'o' sobre la salida reservada es rechazado (carrera archivo/tarea)" || fail "o sobre ruta reservada"
SAL=$(editor doc.txt $'z doc.txt\nq')
echo "$SAL" | grep -q "no puede ser el archivo abierto" && pass "la salida no puede ser el archivo que se edita" || fail "salida == archivo abierto"
SAL=$(editor doc.txt $'z\nw\nx doc.txt.huf doc.txt\nx doc.txt.huf otro.txt\nw\nq')
echo "$SAL" | grep -q "es el archivo abierto" && pass "'x' rechaza sobrescribir el archivo abierto" || fail "x sobre archivo abierto"
[ "$(md5sum < "$T/otro.txt" 2>/dev/null)" = "$(md5sum < "$T/doc.txt")" ] && pass "'x' a otro nombre produce una copia idéntica" || fail "x a otro nombre"
SAL=$(editor doc.txt $'x doc.txt.huf otro.txt\nq')
echo "$SAL" | grep -q "ya existe" && pass "'x' no sobrescribe archivos existentes" || fail "x sobrescribe"
rm -f "$T"/doc.txt.huf* "$T/otro.txt"


# z no sobrescribe archivos existentes (mismo criterio que 'x')
echo "contenido previo" > "$T/previo.huf"
SAL=$(editor doc.txt $'z previo.huf\nq')
echo "$SAL" | grep -q "ya existe" && [ "$(cat "$T/previo.huf")" = "contenido previo" ] && pass "'z' no sobrescribe un archivo de salida existente" || fail "z sobrescribe"
rm -f "$T/previo.huf"

# Alias de ruta: "./salida.huf" debe reconocerse como la misma ruta reservada aunque aún no exista
SAL=$(EAFIT_HUF_RETARDO_MS=40 editor doc.txt $'z salida_alias.huf\no ./salida_alias.huf\nz ./salida_alias.huf\nc\nw\nq')
echo "$SAL" | grep -q "en uso por la tarea" && echo "$SAL" | grep -q "reservado por otra tarea" && [ ! -e "$T/salida_alias.huf" ] && pass "alias de ruta (./x) detectado por ruta canónica (o/z rechazados)" || fail "alias de ruta no detectado"
rm -f "$T"/salida_alias.huf*

# .huf dañado desde el editor
cp "$T/mal.huf" "$T/danado.huf"
SAL=$(editor doc.txt $'x danado.huf salida_mala.txt\nw\nq')
echo "$SAL" | grep -q "\[bg\] Error" && [ ! -e "$T/salida_mala.txt" ] && pass "descompresión de archivo dañado: error en segundo plano, sin salida" || fail "x dañado"

# Los comandos originales del Parcial 1 siguen intactos
printf 'uno\ndos\ntres\n' > "$T/p1.txt"
SAL=$(editor p1.txt $'a cuatro\ni 2 nueva\nd 1\ns tres\np\nq')
echo "$SAL" | grep -q "nueva" && echo "$SAL" | grep -q "cuatro" && [ "$(cat "$T/p1.txt" | tr '\n' ',')" = "nueva,dos,tres,cuatro," ] && pass "regresión: o/p/a/d/i/s/q siguen funcionando" || fail "regresión Parcial 1"

if [ "${RAPIDO:-0}" != "1" ]; then
  echo "== 5. Sanitizers y Valgrind =="
  FLAGS="-g -O1 -std=gnu99 -D_GNU_SOURCE -pthread -Wall -Wextra"
  SRCS="main.c cat_datos.c cat_memoria.c cat_monitoreo.c cat_util.c cat_edicion.c estructura.c modo_comando.c huffman.c tarea_bg.c"
  head -c 3000000 "$T/texto.txt" > "$T/mini.txt"
  if gcc $FLAGS -fsanitize=thread $SRCS -o "$T/eafit_tsan" 2>/dev/null; then
    SAL=$( cd "$T" && printf 'editor mini.txt\nz\na x\nd 1\nw\nx mini.txt.huf mini.rec\nw\nq\nexit\n' | EAFIT_HUF_RETARDO_MS=2 timeout 300 "$T/eafit_tsan" 2>&1 | limpio )
    echo "$SAL" | grep -q "ThreadSanitizer" && fail "ThreadSanitizer reporta carreras de datos" || pass "ThreadSanitizer: 0 carreras de datos (editor + pool + tarea)"
  else echo "  (omitido: sin soporte -fsanitize=thread)"; fi
  if gcc $FLAGS -fsanitize=address,undefined $SRCS -o "$T/eafit_asan" 2>/dev/null; then
    SAL=$( cd "$T" && rm -f mini.txt.huf mini.rec && printf 'editor mini.txt\nz\nw\nx mini.txt.huf mini.rec\nw\nz mini2.huf\nc\nw\nq\nexit\n' | timeout 300 "$T/eafit_asan" 2>&1 | limpio )
    echo "$SAL" | grep -qE "AddressSanitizer|runtime error|LeakSanitizer" && fail "ASan/UBSan/LSan reportan errores" || pass "AddressSanitizer + UBSan + LeakSanitizer: sin errores ni fugas"
  else echo "  (omitido: sin soporte -fsanitize=address)"; fi
  if command -v valgrind >/dev/null; then
    SAL=$( cd "$T" && rm -f mini.txt.huf mini.rec && printf 'editor mini.txt\nz\nw\nx mini.txt.huf mini.rec\nw\nq\nexit\n' | timeout 600 valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=9 "$SHELL_BIN" 2>&1 | limpio )
    echo "$SAL" | grep -q "ERROR SUMMARY: 0 errors" && echo "$SAL" | grep -q "All heap blocks were freed" && pass "Valgrind: 0 errores, 0 fugas (todos los bloques liberados)" || { fail "Valgrind reporta problemas"; echo "$SAL" | tail -15; }
  else echo "  (omitido: valgrind no instalado)"; fi
fi

echo
echo "Resultado: $OK PASS, $FALLOS FAIL"
[ "$FALLOS" -eq 0 ]
