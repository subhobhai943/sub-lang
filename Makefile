# SUB Language Compiler Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Isrc/include -Isrc/core -Isrc/codegen -Isrc/ir -I.
LDFLAGS = 

# Source files for the main compiler/transpiler (sub)
COMPILER_SRC = src/compilers/sub.c src/core/lexer.c src/core/parser_enhanced.c src/core/semantic.c src/core/type_system.c src/codegen/codegen.c src/codegen/codegen_multilang.c src/codegen/codegen_rust.c src/codegen/targets.c src/core/utils.c
COMPILER_OBJ = $(COMPILER_SRC:.c=.o)
COMPILER_TARGET = sub

# Source files for the native compiler (subc)
NATIVE_COMPILER_SRC = src/compilers/sub_native.c src/core/lexer.c src/core/parser_enhanced.c src/core/semantic.c src/ir/ir.c src/codegen/codegen_native.c src/core/utils.c
NATIVE_COMPILER_OBJ = $(NATIVE_COMPILER_SRC:.c=.o)
NATIVE_COMPILER_TARGET = subc

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
endif

.PHONY: all clean compiler native_compiler help

# Default target - build both
all: compiler native_compiler
	@echo "Build complete!"
	@echo "Compiler/Transpiler: ./$(COMPILER_TARGET)"
	@echo "Native Compiler: ./$(NATIVE_COMPILER_TARGET)"

# Main compiler/transpiler
compiler: $(COMPILER_TARGET)

$(COMPILER_TARGET): $(COMPILER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Native compiler
native_compiler: $(NATIVE_COMPILER_TARGET)

$(NATIVE_COMPILER_TARGET): $(NATIVE_COMPILER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(COMPILER_OBJ) $(NATIVE_COMPILER_OBJ)
	@rm -f $(COMPILER_TARGET) $(NATIVE_COMPILER_TARGET)
	@rm -f *.o
	@echo "Clean complete."

# Help
help:
	@echo "SUB Language Build System"
	@echo "--------------------------"
	@echo "Targets:"
	@echo "  all              - Build both the compiler/transpiler and the native compiler (default)"
	@echo "  compiler         - Build the main compiler/transpiler (sub)"
	@echo "  native_compiler  - Build the native compiler (subc)"
	@echo "  clean            - Remove build artifacts"
	@echo "  help             - Show this help message"