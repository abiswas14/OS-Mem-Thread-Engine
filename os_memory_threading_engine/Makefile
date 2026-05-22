CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS := -pthread
SRC := src/main.c src/vm.c src/transport.c
BIN := bin/systems_engine

all: $(BIN)

$(BIN): $(SRC) include/vm.h include/transport.h
	mkdir -p bin
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

run: $(BIN)
	./$(BIN) all

clean:
	rm -rf bin

.PHONY: all run clean
