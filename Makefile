# SUB Language Compiler Makefile
# Builds both standard and multi-language compilers
# Cross-platform compatible

# Detect OS
ifeq ($(OS),Windows_NT)
    detected_OS := Windows
    RM = del /Q
    TARGET_BASIC = sub.exe
    TARGET_MULTI = sublang.exe
    MKDIR = if not exist
    INSTALL_DIR = C:\\Program Files\\SUB
else
    detected_OS := $(shell uname -s)
    RM = rm -f
    TARGET_BASIC = sub
    TARGET_MULTI = sublang
    MKDIR = mkdir -p
    INSTALL_DIR = /usr/local/bin
endif

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11

# Source files for basic compiler (platform-specific)
SOURCES_BASIC = sub.c lexer.c parser.c semantic.c codegen.c utils.c
OBJECTS_BASIC = $(SOURCES_BASIC:.c=.o)

# Source files for multi-language compiler
SOURCES_MULTI = sub_multilang.c lexer.c parser.c semantic.c codegen.c codegen_multilang.c utils.c
OBJECTS_MULTI = $(SOURCES_MULTI:.c=_multi.o)

# Headers
HEADERS = sub_compiler.h error_handler.h

.PHONY: all basic multi clean install install-basic install-multi test help

# Default: build both compilers
all: basic multi
	@echo ""
	@echo "╔═══════════════════════════════════════════╗"
	@echo "║   SUB Language Compilers Built! ✓         ║"
	@echo "╚═══════════════════════════════════════════╝"
	@echo ""
	@echo "Available compilers:"
	@echo "  1. $(TARGET_BASIC)     - Platform-specific compiler (Android, iOS, Web, etc.)"
	@echo "  2. $(TARGET_MULTI)  - Multi-language compiler (Python, Java, Rust, etc.)"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET_BASIC) program.sb linux          # Compile for Linux"
	@echo "  ./$(TARGET_MULTI) program.sb python      # Compile to Python"
	@echo "  ./$(TARGET_MULTI) program.sb rust        # Compile to Rust"
	@echo ""

# Build basic platform-specific compiler
basic: $(TARGET_BASIC)
	@echo "✓ Basic compiler built: $(TARGET_BASIC)"

$(TARGET_BASIC): $(OBJECTS_BASIC)
	$(CC) $(CFLAGS) -o $(TARGET_BASIC) $(OBJECTS_BASIC)

# Build multi-language compiler
multi: $(TARGET_MULTI)
	@echo "✓ Multi-language compiler built: $(TARGET_MULTI)"

$(TARGET_MULTI): $(OBJECTS_MULTI)
	$(CC) $(CFLAGS) -o $(TARGET_MULTI) $(SOURCES_MULTI)

# Compile object files for basic compiler
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile object files for multi-language compiler
%_multi.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
ifeq ($(OS),Windows_NT)
	-$(RM) *.o $(TARGET_BASIC) $(TARGET_MULTI) 2>nul
	-$(RM) output*.* SubProgram.* 2>nul
	@echo Clean complete.
else
	-$(RM) $(OBJECTS_BASIC) $(OBJECTS_MULTI) $(TARGET_BASIC) $(TARGET_MULTI)
	-$(RM) output*.* SubProgram.*
	@echo "✓ Clean complete."
endif

# Install both compilers
install: install-basic install-multi
	@echo ""
	@echo "✓ Both compilers installed successfully!"

install-basic: $(TARGET_BASIC)
ifeq ($(OS),Windows_NT)
	$(MKDIR) "$(INSTALL_DIR)"
	copy $(TARGET_BASIC) "$(INSTALL_DIR)"
	@echo SUB basic compiler installed to $(INSTALL_DIR)
else
	$(MKDIR) $(INSTALL_DIR)
	cp $(TARGET_BASIC) $(INSTALL_DIR)/
	@echo "✓ SUB basic compiler installed to $(INSTALL_DIR)/$(TARGET_BASIC)"
endif

install-multi: $(TARGET_MULTI)
ifeq ($(OS),Windows_NT)
	$(MKDIR) "$(INSTALL_DIR)"
	copy $(TARGET_MULTI) "$(INSTALL_DIR)"
	@echo SUB multi-language compiler installed to $(INSTALL_DIR)
else
	$(MKDIR) $(INSTALL_DIR)
	cp $(TARGET_MULTI) $(INSTALL_DIR)/
	@echo "✓ SUB multi-language compiler installed to $(INSTALL_DIR)/$(TARGET_MULTI)"
endif

# Test targets
test: test-basic test-multi

test-basic: $(TARGET_BASIC)
	@echo "Testing basic compiler..."
ifeq ($(OS),Windows_NT)
	@echo "Testing not yet configured for Windows"
else
	@if [ -f test_real.sb ]; then \
		./$(TARGET_BASIC) test_real.sb linux; \
		if [ -f output_linux.c ]; then \
			echo "✓ Basic compiler test passed"; \
		fi; \
	fi
endif

test-multi: $(TARGET_MULTI)
	@echo "Testing multi-language compiler..."
ifeq ($(OS),Windows_NT)
	@echo "Testing not yet configured for Windows"
else
	@if [ -f test_real.sb ]; then \
		./$(TARGET_MULTI) test_real.sb python; \
		if [ -f output.py ]; then \
			echo "✓ Multi-language compiler test passed"; \
			python3 output.py; \
		fi; \
	fi
endif

# Quick test - compile to multiple languages
test-all-languages: $(TARGET_MULTI)
	@echo "Compiling test program to all supported languages..."
	@if [ -f test_real.sb ]; then \
		for lang in c cpp python java swift kotlin rust javascript css assembly; do \
			echo "→ Compiling to $$lang..."; \
			./$(TARGET_MULTI) test_real.sb $$lang; \
		done; \
		echo ""; \
		echo "✓ All language targets compiled!"; \
		ls -lh output*.* SubProgram.* 2>/dev/null || true; \
	else \
		echo "Error: test_real.sb not found"; \
	fi

help:
	@echo ""
	@echo "╔═══════════════════════════════════════════════════════════╗"
	@echo "║          SUB Language Compiler - Build System            ║"
	@echo "╚═══════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Detected OS: $(detected_OS)"
	@echo ""
	@echo "Build Targets:"
	@echo "  make                    - Build both compilers (default)"
	@echo "  make basic              - Build basic platform compiler only"
	@echo "  make multi              - Build multi-language compiler only"
	@echo ""
	@echo "Maintenance:"
	@echo "  make clean              - Remove all build files"
	@echo "  make install            - Install both compilers"
	@echo "  make install-basic      - Install basic compiler only"
	@echo "  make install-multi      - Install multi-language compiler only"
	@echo ""
	@echo "Testing:"
	@echo "  make test               - Test both compilers"
	@echo "  make test-basic         - Test basic compiler"
	@echo "  make test-multi         - Test multi-language compiler"
	@echo "  make test-all-languages - Compile to all supported languages"
	@echo ""
	@echo "Basic Compiler Usage:"
	@echo "  ./$(TARGET_BASIC) <file.sb> [platform]"
	@echo "  Platforms: linux, windows, macos, android, ios, web"
	@echo ""
	@echo "Multi-Language Compiler Usage:"
	@echo "  ./$(TARGET_MULTI) <file.sb> [language]"
	@echo "  Languages: c, cpp, python, java, swift, kotlin, rust,"
	@echo "             javascript, typescript, assembly, css"
	@echo ""
	@echo "Examples:"
	@echo "  ./$(TARGET_BASIC) app.sb android     → output_android.java"
	@echo "  ./$(TARGET_MULTI) app.sb python   → output.py"
	@echo "  ./$(TARGET_MULTI) app.sb rust     → output.rs"
	@echo ""
