CC = gcc
CFLAGS = -Wall -Wextra -g -D_GNU_SOURCE
LDFLAGS = -pthread

TARGET = pacman_concurrente

SOURCES = main.c map.c pacman.c ghost.c collision.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

run:
	./$(TARGET) Caso1

