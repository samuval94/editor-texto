#!/bin/bash
EDITOR=./editor
ARCHIVO=test_validacion.txt
FALLOS=0
TOTAL=0

assert_contains() {
    local descripcion="$1"
    local patron="$2"
    local salida="$3"
    TOTAL=$((TOTAL + 1))
    if echo "$salida" | grep -qF -- "$patron"; then
        echo "  [PASS] $descripcion"
    else
        echo "  [FAIL] $descripcion"
        echo "         Se esperaba encontrar: '$patron'"
        FALLOS=$((FALLOS + 1))
    fi
}

assert_file_contains() {
    local descripcion="$1"
    local patron="$2"
    local archivo="$3"
    TOTAL=$((TOTAL + 1))
    if grep -qF -- "$patron" "$archivo" 2>/dev/null; then
        echo "  [PASS] $descripcion"
    else
        echo "  [FAIL] $descripcion (archivo: $archivo)"
        FALLOS=$((FALLOS + 1))
    fi
}

assert_file_not_contains() {
    local descripcion="$1"
    local patron="$2"
    local archivo="$3"
    TOTAL=$((TOTAL + 1))
    if ! grep -qF -- "$patron" "$archivo" 2>/dev/null; then
        echo "  [PASS] $descripcion"
    else
        echo "  [FAIL] $descripcion (archivo: $archivo)"
        FALLOS=$((FALLOS + 1))
    fi
}

assert_exit_ok() {
    local descripcion="$1"
    local codigo="$2"
    TOTAL=$((TOTAL + 1))
    if [ "$codigo" -eq 0 ]; then
        echo "  [PASS] $descripcion"
    else
        echo "  [FAIL] $descripcion (código de salida: $codigo)"
        FALLOS=$((FALLOS + 1))
    fi
}

rm -f "$ARCHIVO"

echo "===== ESCENARIO 1: crear archivo nuevo y agregar líneas (comando 'a') ====="
SALIDA=$(printf 'o %s\na Universidad EAFIT\na Sistemas Operativos\na Proyecto Editor de Texto\nq\n' "$ARCHIVO" | $EDITOR 2>&1)
CODIGO=$?
assert_exit_ok "El programa termina sin errores" "$CODIGO"
assert_file_contains "El archivo en disco contiene 'Universidad EAFIT'" "Universidad EAFIT" "$ARCHIVO"
assert_file_contains "El archivo en disco contiene 'Proyecto Editor de Texto'" "Proyecto Editor de Texto" "$ARCHIVO"
echo

echo "===== ESCENARIO 2: persistencia — reabrir e imprimir (comandos 'o' y 'p') ====="
SALIDA=$(printf 'o %s\np 2\nq\n' "$ARCHIVO" | $EDITOR 2>&1)
assert_contains "Al imprimir la línea 2 se ve 'Sistemas Operativos'" "Sistemas Operativos" "$SALIDA"
echo

echo "===== ESCENARIO 3: insertar en medio (comando 'i') ====="
SALIDA=$(printf 'o %s\ni 2 Semestre 2026-B\np\nq\n' "$ARCHIVO" | $EDITOR 2>&1)
assert_contains "La línea insertada aparece en la posición 2" "2| Semestre 2026-B" "$SALIDA"
assert_file_contains "El archivo en disco quedó con el texto insertado" "Semestre 2026-B" "$ARCHIVO"
echo

echo "===== ESCENARIO 4: borrar línea y truncar (comando 'd') ====="
SALIDA=$(printf 'o %s\nd 1\np\nq\n' "$ARCHIVO" | $EDITOR 2>&1)
assert_file_not_contains "La línea borrada ('Universidad EAFIT') ya no está en disco" "Universidad EAFIT" "$ARCHIVO"
echo

echo "===== ESCENARIO 5: búsqueda de palabra (comando 's') ====="
SALIDA=$(printf 'o %s\ns Semestre\nq\n' "$ARCHIVO" | $EDITOR 2>&1)
assert_contains "La búsqueda encuentra la palabra 'Semestre'" "encontrada" "$SALIDA"
SALIDA_NEG=$(printf 'o %s\ns PalabraQueNoExiste\nq\n' "$ARCHIVO" | $EDITOR 2>&1)
assert_contains "La búsqueda de una palabra inexistente lo informa correctamente" "no se encontró" "$SALIDA_NEG"
echo

echo "===== ESCENARIO 6: robustez — comandos sin archivo abierto ====="
SALIDA=$(printf 'p\nd 1\ni 1 texto\ns palabra\nq\n' | $EDITOR 2>&1)
CODIGO=$?
assert_exit_ok "El programa no se cae al usar comandos sin abrir archivo" "$CODIGO"
assert_contains "Se informa el error de 'archivo no abierto'" "No hay ningún archivo abierto" "$SALIDA"
echo

echo "===== ESCENARIO 7: robustez — números de línea fuera de rango ====="
SALIDA=$(printf 'o %s\np 9999\nd 9999\nq\n' "$ARCHIVO" | $EDITOR 2>&1)
CODIGO=$?
assert_exit_ok "El programa no se cae con números de línea inválidos" "$CODIGO"
assert_contains "Se informa que la línea no existe" "no existe" "$SALIDA"
echo

echo "===== ESCENARIO 8: robustez — comando desconocido ====="
SALIDA=$(printf 'o %s\nx\nq\n' "$ARCHIVO" | $EDITOR 2>&1)
CODIGO=$?
assert_exit_ok "El programa no se cae con un comando inválido" "$CODIGO"
assert_contains "Se informa 'Comando desconocido'" "Comando desconocido" "$SALIDA"
echo

echo "===== ESCENARIO 9: archivo vacío recién creado (0 líneas) ====="
rm -f vacio_test.txt
SALIDA=$(printf 'o vacio_test.txt\np\nq\n' | $EDITOR 2>&1)
CODIGO=$?
assert_exit_ok "Abrir y listar un archivo vacío no causa error" "$CODIGO"
rm -f vacio_test.txt
echo

rm -f "$ARCHIVO"

echo "============================================================"
echo "RESULTADO FINAL: $((TOTAL - FALLOS))/$TOTAL pruebas pasaron."
if [ "$FALLOS" -eq 0 ]; then
    echo "TODAS LAS PRUEBAS PASARON CORRECTAMENTE."
    exit 0
else
    echo "$FALLOS prueba(s) fallaron. Revisar arriba."
    exit 1
fi