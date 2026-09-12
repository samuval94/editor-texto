CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE
ARCHSALIDA = eafitOS
SRCS = main.c cat_datos.c cat_memoria.c cat_monitoreo.c cat_util.c cat_edicion.c estructura.c modo_comando.c
OBJS = $(SRCS:.c=.o)

all: $(ARCHSALIDA)

$(ARCHSALIDA): $(OBJS)
	$(CC) $(CFLAGS) -o $(ARCHSALIDA) $(OBJS)

# 'make run' compila (si hace falta) y luego lanza el shell interactivo.
# Se separó del target 'all' para que 'make'/'make all' solo compile,
# sin bloquear la terminal esperando comandos (comportamiento esperado
# de un Makefile: compilar, no ejecutar).
run: $(ARCHSALIDA)
	./$(ARCHSALIDA)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(ARCHSALIDA) $(OBJS)

.PHONY: all run clean
