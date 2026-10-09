CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE -pthread
ARCHSALIDA = eafitOS
SRCS = main.c cat_datos.c cat_memoria.c cat_monitoreo.c cat_util.c cat_edicion.c estructura.c modo_comando.c huffman.c tarea_bg.c
OBJS = $(SRCS:.c=.o)

all: $(ARCHSALIDA) huf

$(ARCHSALIDA): $(OBJS)
	$(CC) $(CFLAGS) -o $(ARCHSALIDA) $(OBJS)

# 'make run' compila (si hace falta) y luego lanza el shell interactivo.
# Se separó del target 'all' para que 'make'/'make all' solo compile,
# sin bloquear la terminal esperando comandos (comportamiento esperado
# de un Makefile: compilar, no ejecutar).
run: $(ARCHSALIDA)
	./$(ARCHSALIDA)

# Cualquier cambio en un .h recompila todo (proyecto pequeño: más seguro que dependencias finas).
HDRS = $(wildcard *.h)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# Utilidad independiente del compresor (Parcial 2): 'huf c|d <entrada> <salida>'.
huf: huf_cli.c huffman.c huffman.h
	$(CC) $(CFLAGS) -o huf huf_cli.c huffman.c

# Ejecuta el script de pruebas del Parcial 2 (integridad, concurrencia, editor).
test: all
	./test_huffman.sh

clean:
	rm -f $(ARCHSALIDA) huf $(OBJS)

.PHONY: all run clean test
