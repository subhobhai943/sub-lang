# SUB Language Compiler Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Isrc/include -Isrc/core -Isrc/codegen -Isrc/ir -I.
LDFLAGS = 

# Source files for the main compiler/transpiler (sub)
COMPILER_SRC = src/compilers/sub.c src/core/interpreter.c src/core/lexer.c src/core/parser_enhanced.c src/core/semantic.c src/core/type_system.c src/codegen/codegen.c src/codegen/codegen_multilang.c src/codegen/codegen_rust.c src/codegen/targets.c src/core/utils.c
COMPILER_OBJ = $(COMPILER_SRC:.c=.o)
COMPILER_TARGET = sub

# Source files for the native compiler (subc) - now uses C backend + gcc
NATIVE_COMPILER_SRC = src/compilers/sub_native.c src/core/interpreter.c src/core/lexer.c src/core/parser_enhanced.c src/core/semantic.c src/core/type_system.c src/codegen/codegen.c src/codegen/codegen_multilang.c src/codegen/codegen_rust.c src/codegen/targets.c src/core/utils.c
NATIVE_COMPILER_OBJ = $(NATIVE_COMPILER_SRC:.c=.o)
NATIVE_COMPILER_TARGET = subc

# Source files for the interpreter (subi)
INTERP_SRC = src/compilers/subi.c src/core/interpreter.c src/core/lexer.c src/core/parser_enhanced.c src/core/semantic.c src/core/type_system.c src/core/utils.c src/codegen/codegen.c src/codegen/codegen_multilang.c src/codegen/codegen_rust.c src/codegen/targets.c
INTERP_OBJ = $(INTERP_SRC:.c=.o)
INTERP_TARGET = subi

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    CFLAGS += -DMACOS
else ifeq ($(UNAME_S),Linux)
    # Linux
    CFLAGS += -DLINUX
else
    # Windows (MinGW/Cygwin/MSYS2)
    CFLAGS += -DWINDOWS
    LDFLAGS += -static
    COMPILER_TARGET = sub.exe
    NATIVE_COMPILER_TARGET = subc.exe
    INTERP_TARGET = subi.exe
endif

.PHONY: all clean compiler native_compiler interpreter help

# Default target - build all three
all: compiler native_compiler interpreter
	@echo "Build complete!"
	@echo "Compiler/Transpiler: ./$(COMPILER_TARGET)"
	@echo "Native Compiler:     ./$(NATIVE_COMPILER_TARGET)"
	@echo "Interpreter:         ./$(INTERP_TARGET)"

# Main compiler/transpiler
compiler: $(COMPILER_TARGET)

$(COMPILER_TARGET): $(COMPILER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Native compiler (now uses C backend + gcc)
native_compiler: $(NATIVE_COMPILER_TARGET)

$(NATIVE_COMPILER_TARGET): $(NATIVE_COMPILER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Interpreter
interpreter: $(INTERP_TARGET)

$(INTERP_TARGET): $(INTERP_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(COMPILER_OBJ) $(NATIVE_COMPILER_OBJ) $(INTERP_OBJ)
	@rm -f $(COMPILER_TARGET) $(NATIVE_COMPILER_TARGET) $(INTERP_TARGET)
	@rm -f sub.exe subc.exe subi.exe
	@rm -f *.o
	@echo "Clean complete."

# Help
help:
	@echo "SUB Language Build System"
	@echo "--------------------------"
	@echo "Targets:"
	@echo "  all              - Build all three tools (default)"
	@echo "  compiler         - Build the main compiler/transpiler (sub)"
	@echo "  native_compiler  - Build the native compiler (subc)"
	@echo "  interpreter      - Build the interpreter (subi)"
	@echo "  clean            - Remove build artifacts"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  ./sub hello.sb python      # transpile to hello.py"
	@echo "  ./subc hello.sb hello      # compile to native binary"
	@echo "  ./subi hello.sb            # interpret directly"
