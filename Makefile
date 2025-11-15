# SUB Language Compiler Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c11
TARGET = sub
SOURCES = sub.c lexer.c parser.c semantic.c codegen.c utils.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = sub_compiler.h

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)
	@echo "Build successful! SUB compiler ready."

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete."

install:
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/
	@echo "SUB compiler installed to /usr/local/bin/$(TARGET)"

test: $(TARGET)
	./run_tests.sh
	@echo "Tests complete."

help:
	@echo "SUB Language Compiler"
	@echo "====================="
	@echo "make        - Build the compiler"
	@echo "make clean  - Remove build files"
	@echo "make install- Install compiler"
	@echo "make test   - Run tests"
