CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g -pthread
CPPFLAGS ?= -Isrc
LDFLAGS ?= -pthread

TARGET := build/pacman_concurrente
SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=build/%.o)

.PHONY: all run clean

all: $(TARGET)

run: all
	./$(TARGET) cases/Caso1

$(TARGET): $(OBJS) | build
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build
