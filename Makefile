CC = gcc
CFLAGS = -Wall -Wextra -g -D_GNU_SOURCE
LDFLAGS = -pthread

TARGET = pacman_concurrente

SOURCES = main.c map.c pacman.c ghost.c collision.c renderer.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

run:
	./$(TARGET) Caso1

# BONUS: ejecuta con el proceso P3 (renderer) habilitado.
# El debug de P0/P1/P2 va a pacman_debug.log y en pantalla queda solo
# la animacion (P3 dibuja en /dev/tty).
run-render:
	./$(TARGET) Caso1 40 --render > pacman_debug.log 2>&1

