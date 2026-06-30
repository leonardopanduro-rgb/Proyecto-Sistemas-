CC = gcc
CFLAGS = -Wall -Wextra -g -D_GNU_SOURCE
LDFLAGS = -pthread

TARGET = pacman_concurrente
BUILD_DIR = build
NORMAL_TARGET = $(BUILD_DIR)/pacman_normal
TSAN_TARGET = $(BUILD_DIR)/pacman_tsan

SOURCES = main.c map.c pacman.c ghost.c collision.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

normal: $(BUILD_DIR)
	$(CC) -Wall -Wextra -g -D_GNU_SOURCE $(SOURCES) -o $(NORMAL_TARGET) -pthread

tsan: $(BUILD_DIR)
	$(CC) -Wall -Wextra -g -D_GNU_SOURCE -fsanitize=thread $(SOURCES) -o $(TSAN_TARGET) -pthread

experiments: normal tsan
	./experiments/run_experiments.sh

clean:
	rm -f $(TARGET) $(NORMAL_TARGET) $(TSAN_TARGET)

run:
	./$(TARGET) Caso1


.PHONY: all clean run normal tsan experiments
