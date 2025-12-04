# SUB Language Compiler Makefile
# CMake wrapper for cross-platform compatibility

# Detect OS
ifeq ($(OS),Windows_NT)
    detected_OS := Windows
    RM = rmdir /S /Q
    MKDIR = mkdir
    TARGET = build\\Release\\subc.exe
    INSTALL_DIR = C:\\Program Files\\SUB
else
    detected_OS := $(shell uname -s)
    RM = rm -rf
    MKDIR = mkdir -p
    TARGET = build/subc
    INSTALL_DIR = /usr/local/bin
endif

.PHONY: all build clean install test help cmake-config

all: build
	@echo "Build successful! SUB compiler ready for $(detected_OS)"

cmake-config:
	@echo "Configuring CMake build system..."
	$(MKDIR) build 2>/dev/null || true
	cmake -B build -DCMAKE_BUILD_TYPE=Release

build: cmake-config
	@echo "Building SUB compiler with CMake..."
	cmake --build build --config Release
	@echo "✓ Build complete"

clean:
	@echo "Cleaning build artifacts..."
ifeq ($(OS),Windows_NT)
	-$(RM) build 2>nul || true
	-del /Q *.o *.exe 2>nul || true
else
	-$(RM) build
	-$(RM) *.o sub output_*.js output_*.java output_*.c
endif
	@echo "Clean complete."

install: build
	@echo "Installing SUB compiler..."
ifeq ($(OS),Windows_NT)
	$(MKDIR) "$(INSTALL_DIR)" 2>nul || true
	copy $(TARGET) "$(INSTALL_DIR)" /Y
	@echo SUB compiler installed to $(INSTALL_DIR)
else
	sudo $(MKDIR) $(INSTALL_DIR)
	sudo cp $(TARGET) $(INSTALL_DIR)/subc
	@echo "SUB compiler installed to $(INSTALL_DIR)/subc"
endif

test: build
	@echo "Running tests..."
ifeq ($(OS),Windows_NT)
	.\\build\\Release\\subc.exe example.sb web
	@echo "Test compilation complete"
else
	./build/subc example.sb web
	@echo "Test compilation complete"
endif

help:
	@echo "SUB Language Compiler (CMake Edition)"
	@echo "======================================="
	@echo "Detected OS: $(detected_OS)"
	@echo ""
	@echo "Available targets:"
	@echo "  make all      - Build the compiler (default)"
	@echo "  make build    - Build with CMake"
	@echo "  make clean    - Remove all build artifacts"
	@echo "  make install  - Install compiler to system"
	@echo "  make test     - Build and test with example.sb"
	@echo "  make help     - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - CMake 3.20+"
	@echo "  - Rust toolchain (stable)"
	@echo "  - C++17 compatible compiler"
	@echo "  - C11 compatible compiler"
