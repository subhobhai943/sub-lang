# SUB Language Compiler Makefile
# Supports both native compilation and transpilation

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -I.
LDFLAGS = 

# Source files for native compiler
NATIVE_SOURCES = sub_native.c lexer.c parser.c semantic.c ir.c codegen_native.c
NATIVE_OBJECTS = $(NATIVE_SOURCES:.c=.o)
NATIVE_TARGET = subc

# Source files for transpiler
TRANS_SOURCES = sub_multilang.c lexer.c parser.c semantic.c codegen.c codegen_multilang.c
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
    # Windows
    CFLAGS += -DWINDOWS
    NATIVE_TARGET = subc.exe
    TRANS_TARGET = sublang.exe
endif

.PHONY: all clean native transpiler install test help

# Default target - build both
all: native transpiler

# Native compiler (machine code)
native: $(NATIVE_TARGET)

$(NATIVE_TARGET): $(NATIVE_OBJECTS)
	@echo "Linking native compiler..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Native compiler built: $(NATIVE_TARGET)"
	@echo "Usage: ./$(NATIVE_TARGET) program.sb"

# Transpiler (multi-language)
transpiler: $(TRANS_TARGET)

$(TRANS_TARGET): $(TRANS_OBJECTS)
	@echo "Linking transpiler..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Transpiler built: $(TRANS_TARGET)"
	@echo "Usage: ./$(TRANS_TARGET) program.sb python"

# Compile C files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Install (optional)
install: all
	@echo "Installing SUB compilers..."
	mkdir -p /usr/local/bin
	cp $(NATIVE_TARGET) /usr/local/bin/
	cp $(TRANS_TARGET) /usr/local/bin/
	@echo "Installed:"
	@echo "  /usr/local/bin/$(NATIVE_TARGET) - Native compiler"
	@echo "  /usr/local/bin/$(TRANS_TARGET) - Transpiler"

# Test suite
test: all
	@echo "Running tests..."
	@echo "[TEST 1] Native compilation"
	@echo '#var x = 5' > test_temp.sb
	@echo '#var y = 10' >> test_temp.sb
	@echo '#var z = x + y' >> test_temp.sb
	./$(NATIVE_TARGET) test_temp.sb -S -o test_temp.s
	@echo "[TEST 2] Python transpilation"
	./$(TRANS_TARGET) test_temp.sb python
	@echo "[TEST 3] JavaScript transpilation"
	./$(TRANS_TARGET) test_temp.sb javascript
	@rm -f test_temp.sb test_temp.s output.*
	@echo "All tests passed!"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(NATIVE_OBJECTS) $(TRANS_OBJECTS)
	rm -f $(NATIVE_TARGET) $(TRANS_TARGET)
	rm -f *.o *.s *.out a.out
	rm -f output.* SubProgram.* test_temp.*
	@echo "Clean complete"

# Help
help:
	@echo "SUB Language Compiler - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build both native compiler and transpiler (default)"
	@echo "  native      - Build native machine code compiler (subc)"
	@echo "  transpiler  - Build multi-language transpiler (sublang)"
	@echo "  install     - Install compilers to /usr/local/bin"
	@echo "  test        - Run test suite"
	@echo "  clean       - Remove build artifacts"
	@echo "  help        - Show this help"
	@echo ""
	@echo "Usage Examples:"
	@echo "  make              # Build everything"
	@echo "  make native       # Build only native compiler"
	@echo "  make transpiler   # Build only transpiler"
	@echo "  make test         # Run tests"
	@echo ""
	@echo "Compiler Usage:"
	@echo "  ./subc program.sb              # Compile to native binary"
	@echo "  ./subc program.sb -O3          # With optimization"
	@echo "  ./subc program.sb -S           # Generate assembly"
	@echo "  ./sublang program.sb python    # Transpile to Python"
	@echo "  ./sublang program.sb java      # Transpile to Java"
