# SUB Language Compiler Makefile

CC = gcc

# gnu11 rather than c11: the sources use POSIX (strdup, strcasecmp, getpid)
# and say so with _GNU_SOURCE. glibc honours that even under -std=c11, but
# MinGW keys off __STRICT_ANSI__ instead and hides those declarations, so a
# strict-ANSI build only ever worked on Linux and macOS.
CFLAGS = -Wall -Wextra -std=gnu11 -O2 -Isrc/include -Isrc/core -Isrc/codegen -Isrc/native -I.
LDFLAGS = -lm

# Extra flags for callers who want to add to the build rather than replace it.
# CI uses `make EXTRA_CFLAGS=-Werror` so it never has to restate the include
# paths - restating them meant adding a source directory silently broke the
# warnings job while the ordinary build kept working.
CFLAGS += $(EXTRA_CFLAGS)

# Source files for the main compiler/transpiler (sub)
COMPILER_SRC = src/compilers/sub.c src/core/interpreter.c src/core/lexer.c src/core/module.c src/core/parser_enhanced.c src/core/semantic.c src/core/type_system.c src/codegen/codegen.c src/codegen/codegen_infer.c src/codegen/codegen_switch.c src/codegen/codegen_globals.c src/codegen/codegen_multilang.c src/codegen/codegen_rust.c src/codegen/codegen_cpp.c src/core/utils.c
COMPILER_OBJ = $(COMPILER_SRC:.c=.o)
COMPILER_TARGET = sub

# Source files for the native compiler (subc). src/native/ is the built-in
# x86-64 backend: it emits machine code and writes the ELF itself, so a
# compiled SUB program needs no C toolchain on the machine that runs subc.
NATIVE_COMPILER_SRC = src/compilers/sub_native.c src/native/x64_emit.c src/native/x64_runtime.c src/native/x64_codegen.c src/native/elf64.c src/core/interpreter.c src/core/lexer.c src/core/module.c src/core/parser_enhanced.c src/core/semantic.c src/core/type_system.c src/codegen/codegen.c src/codegen/codegen_infer.c src/codegen/codegen_switch.c src/codegen/codegen_globals.c src/codegen/codegen_multilang.c src/codegen/codegen_rust.c src/codegen/codegen_cpp.c src/core/utils.c
NATIVE_COMPILER_OBJ = $(NATIVE_COMPILER_SRC:.c=.o)
NATIVE_COMPILER_TARGET = subc

# Source files for the interpreter (subi)
INTERP_SRC = src/compilers/subi.c src/core/interpreter.c src/core/lexer.c src/core/module.c src/core/parser_enhanced.c src/core/semantic.c src/core/type_system.c src/core/utils.c src/codegen/codegen.c src/codegen/codegen_infer.c src/codegen/codegen_switch.c src/codegen/codegen_globals.c src/codegen/codegen_multilang.c src/codegen/codegen_rust.c src/codegen/codegen_cpp.c
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

.PHONY: all clean compiler native_compiler interpreter install uninstall help

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

# -MMD -MP records which headers each object used, so editing a header
# rebuilds what depends on it. Without this a change to an enum in a shared
# header left already-built objects using the old numbering, and the two
# halves of the program disagreed about what the values meant.
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

DEPS = $(COMPILER_OBJ:.o=.d) $(NATIVE_COMPILER_OBJ:.o=.d) $(INTERP_OBJ:.o=.d)
-include $(DEPS)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(COMPILER_OBJ) $(NATIVE_COMPILER_OBJ) $(INTERP_OBJ)
	@rm -f $(COMPILER_OBJ:.o=.d) $(NATIVE_COMPILER_OBJ:.o=.d) $(INTERP_OBJ:.o=.d)
	@rm -f $(COMPILER_TARGET) $(NATIVE_COMPILER_TARGET) $(INTERP_TARGET)
	@rm -f sub.exe subc.exe subi.exe
	@rm -f *.o
	@echo "Clean complete."

# Install. The binaries go in bin/ and the standard library in
# lib/sub/stdlib, which is the second of the two places the module resolver
# looks for it relative to the executable -- so `import "math"` works from
# anywhere after this.
PREFIX ?= /usr/local

install: all
	@install -d "$(DESTDIR)$(PREFIX)/bin"
	@install -m 755 sub subc subi "$(DESTDIR)$(PREFIX)/bin/"
	@install -d "$(DESTDIR)$(PREFIX)/lib/sub/stdlib"
	@install -m 644 stdlib/*.sb "$(DESTDIR)$(PREFIX)/lib/sub/stdlib/"
	@echo "Installed to $(DESTDIR)$(PREFIX)"

uninstall:
	@rm -f "$(DESTDIR)$(PREFIX)/bin/sub" \
	       "$(DESTDIR)$(PREFIX)/bin/subc" \
	       "$(DESTDIR)$(PREFIX)/bin/subi"
	@rm -rf "$(DESTDIR)$(PREFIX)/lib/sub"
	@echo "Removed from $(DESTDIR)$(PREFIX)"

# Help
help:
	@echo "SUB Language Build System"
	@echo "--------------------------"
	@echo "Targets:"
	@echo "  all              - Build all three tools (default)"
	@echo "  compiler         - Build the main compiler/transpiler (sub)"
	@echo "  native_compiler  - Build the native compiler (subc)"
	@echo "  interpreter      - Build the interpreter (subi)"
	@echo "  install          - Install to PREFIX (default /usr/local)"
	@echo "  uninstall        - Remove an installation"
	@echo "  clean            - Remove build artifacts"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  ./sub hello.sb python      # transpile to hello.py"
	@echo "  ./subc hello.sb hello      # compile to native binary"
	@echo "  ./subi hello.sb            # interpret directly"
