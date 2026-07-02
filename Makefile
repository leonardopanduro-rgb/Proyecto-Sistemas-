CC = gcc
CFLAGS = -Wall -Wextra -g -D_GNU_SOURCE
LDFLAGS = -pthread

TARGET = pacman_concurrente

# Fuentes comunes (todo menos el renderer de P3, que se elige con RENDER).
BASE_SOURCES = main.c map.c pacman.c ghost.c
HEADERS = shared.h map.h pacman.h ghost.h renderer.h

# Selector del renderer de P3:
#   make              -> consola con ncurses  (renderer.c)      [por defecto]
#   make RENDER=sdl   -> ventana grafica SDL2 (renderer_sdl.c)
# Al cambiar de RENDER ejecuta antes 'make clean' (el binario se llama igual).
RENDER ?= console

ifeq ($(RENDER),sdl)
    RENDER_SRC  = renderer_sdl.c
    RENDER_LIBS = $(shell sdl2-config --cflags --libs)
else
    RENDER_SRC  = renderer.c
    RENDER_LIBS = -lncurses
endif

SOURCES = $(BASE_SOURCES) $(RENDER_SRC)

.PHONY: all normal tsan clean run run-render run-sdl

all: $(TARGET)

normal: clean $(TARGET)

tsan: CFLAGS += -fsanitize=thread
tsan: LDFLAGS += -fsanitize=thread
tsan: clean $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS) $(RENDER_LIBS)

clean:
	rm -f $(TARGET)

run:
	./$(TARGET) Caso1

# Consola (ncurses): debug a archivo, animacion en pantalla.
run-render:
	./$(TARGET) Caso1 40 --render > pacman_debug.log 2>&1

# Ventana SDL2 (antes: make clean && make RENDER=sdl).
run-sdl:
	./$(TARGET) Caso1 40 --render
