# SUB Language Compiler Makefile
# Supports both native compilation and transpilation

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -I.
LDFLAGS = 

# Source files for native compiler
NATIVE_SOURCES = sub_native_compiler.c lexer.c parser_enhanced.c semantic.c ir.c codegen_x64.c utils.c
NATIVE_OBJECTS = $(NATIVE_SOURCES:.c=.o)
NATIVE_TARGET = subc-native

# Source files for transpiler
TRANS_SOURCES = sub_multilang.c lexer.c parser_enhanced.c semantic.c codegen.c codegen_multilang.c codegen_rust.c type_system.c error_handler.c codegen_cpp.c targets.c
TRANS_OBJECTS = $(TRANS_SOURCES:.c=.o)
TRANS_TARGET = sublang

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
    NATIVE_TARGET = subc-native.exe
    TRANS_TARGET = sublang.exe
endif

.PHONY: all clean native transpiler install test help

# Default target - build both
all: native transpiler
	@echo ""
	@echo "✅ Build complete!"
	@echo ""
	@echo "Native Compiler: ./$(NATIVE_TARGET)"
	@echo "  Usage: ./$(NATIVE_TARGET) program.sb [output]"
	@echo "  Example: ./$(NATIVE_TARGET) example.sb myapp"
	@echo ""
	@echo "Transpiler: ./$(TRANS_TARGET)"
	@echo "  Usage: ./$(TRANS_TARGET) program.sb <language>"
	@echo "  Example: ./$(TRANS_TARGET) example.sb python"
	@echo ""

# Native compiler (machine code)
native: $(NATIVE_TARGET)
	@echo ""
	@echo "✅ Native compiler ready!"
	@echo "   ./$(NATIVE_TARGET) program.sb"

$(NATIVE_TARGET): $(NATIVE_OBJECTS)
	@echo "🔗 Linking native compiler..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Transpiler (multi-language)
transpiler: $(TRANS_TARGET)
	@echo ""
	@echo "✅ Transpiler ready!"
	@echo "   ./$(TRANS_TARGET) program.sb python"

$(TRANS_TARGET): $(TRANS_OBJECTS)
	@echo "🔗 Linking transpiler..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile C files
%.o: %.c
	@echo "⚙️  Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Install (optional)
install: all
	@echo "Installing SUB compilers..."
ifeq ($(OS),Windows_NT)
	@echo "Windows: Copy executables to current directory or add to PATH manually"
else
	mkdir -p /usr/local/bin
	cp $(NATIVE_TARGET) /usr/local/bin/subc
	cp $(TRANS_TARGET) /usr/local/bin/sublang
	@echo "✅ Installed:"
	@echo "   /usr/local/bin/subc     - Native compiler"
	@echo "   /usr/local/bin/sublang  - Transpiler"
endif

# Test suite
test: all
	@echo ""
	@echo "🧪 Running test suite..."
	@echo ""
	@echo "[TEST 1] Creating test program..."
	@echo '#var x = 5' > test_temp.sb
	@echo '#var y = 10' >> test_temp.sb
	@echo '#print(x + y)' >> test_temp.sb
	@echo "✓ Test file created"
	@echo ""
	@echo "[TEST 2] Native compilation..."
	./$(NATIVE_TARGET) test_temp.sb test_output && echo "✓ Native compilation passed"
	@echo ""
	@echo "[TEST 3] Python transpilation test: $(TRANS_TARGET)"
	@echo "Running comprehensive verification..."
	@python3 tests/run_tests.py
	@echo ""
	@echo "[TEST 4] JavaScript transpilation..."
	./$(TRANS_TARGET) test_temp.sb javascript && echo "✓ JavaScript transpilation passed"
	@echo ""
	@rm -f test_temp.sb test_output* output.*
	@echo "✅ All tests passed!"
	@echo ""

# Quick test with example
example: all
	@echo "🚀 Compiling example.sb..."
	@echo ""
	@echo "[1] Native compilation:"
	./$(NATIVE_TARGET) example.sb example_native
	@echo ""
	@echo "[2] Python transpilation:"
	./$(TRANS_TARGET) example.sb python
	@echo ""

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	@rm -f $(NATIVE_OBJECTS) $(TRANS_OBJECTS)
	@rm -f $(NATIVE_TARGET) $(TRANS_TARGET)
	@rm -f subc subc.exe sublang.exe
	@rm -f *.o *.s *.out a.out
	@rm -f output.* SubProgram.* test_temp.* test_output*
	@echo "✓ Clean complete"

# Help
help:
	@echo "╭────────────────────────────────────────────────────╮"
	@echo "│  SUB Language Compiler - Build System        │"
	@echo "╰────────────────────────────────────────────────────╯"
	@echo ""
	@echo "🎯 Build Targets:"
	@echo "  all         - Build both compilers (default)"
	@echo "  native      - Build native machine code compiler"
	@echo "  transpiler  - Build multi-language transpiler"
	@echo "  install     - Install to /usr/local/bin"
	@echo "  test        - Run test suite"
	@echo "  example     - Compile example.sb"
	@echo "  clean       - Remove build artifacts"
	@echo "  help        - Show this help"
	@echo ""
	@echo "💻 Usage Examples:"
	@echo "  make              # Build everything"
	@echo "  make native       # Build native compiler only"
	@echo "  make test         # Run tests"
	@echo "  make example      # Try with example.sb"
	@echo ""
	@echo "⚙️  Compiler Usage:"
	@echo "  Native:     ./$(NATIVE_TARGET) program.sb [output]"
	@echo "  Transpile:  ./$(TRANS_TARGET) program.sb <language>"
	@echo ""
	@echo "  Languages: python, javascript, java, ruby, rust, etc."
	@echo ""
