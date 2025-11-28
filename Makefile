# SUB Language Compiler Makefile
# Cross-platform compatible

# Detect OS
ifeq ($(OS),Windows_NT)
    detected_OS := Windows
    RM = del /Q
    TARGET = sub.exe
    MKDIR = if not exist
    INSTALL_DIR = C:\\Program Files\\SUB
else
    detected_OS := $(shell uname -s)
    RM = rm -f
    TARGET = sub
    MKDIR = mkdir -p
    INSTALL_DIR = /usr/local/bin
endif

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
SOURCES = sub.c lexer.c parser.c semantic.c codegen.c utils.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = sub_compiler.h error_handler.h

.PHONY: all clean install test help

all: $(TARGET)
	@echo "Build successful! SUB compiler ready for $(detected_OS)"

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
ifeq ($(OS),Windows_NT)
	-$(RM) *.o $(TARGET) 2>nul
	@echo Clean complete.
else
	-$(RM) $(OBJECTS) $(TARGET)
	@echo "Clean complete."
endif

install: $(TARGET)
ifeq ($(OS),Windows_NT)
	$(MKDIR) "$(INSTALL_DIR)"
	copy $(TARGET) "$(INSTALL_DIR)"
	@echo SUB compiler installed to $(INSTALL_DIR)
else
	$(MKDIR) $(INSTALL_DIR)
	cp $(TARGET) $(INSTALL_DIR)/
	@echo "SUB compiler installed to $(INSTALL_DIR)/$(TARGET)"
endif

test: $(TARGET)
ifeq ($(OS),Windows_NT)
	@echo "Testing not yet configured for Windows"
else
	@if [ -f run_tests.sh ]; then \
		./run_tests.sh; \
		echo "Tests complete."; \
	else \
		echo "No test script found"; \
	fi
endif

help:
	@echo "SUB Language Compiler"
	@echo "====================="
	@echo "Detected OS: $(detected_OS)"
	@echo ""
	@echo "make        - Build the compiler"
	@echo "make clean  - Remove build files"
	@echo "make install- Install compiler"
	@echo "make test   - Run tests"
	@echo "make help   - Show this help"
