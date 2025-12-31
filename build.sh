#!/bin/bash
# SUB Language Heavy Compiler Build Script
# Orchestrates Rust + C++ + C compilation

set -e

BOLD='\033[1m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BOLD}${BLUE}"
echo "═══════════════════════════════════════════════════════════"
echo "  SUB Language Heavy Compiler Build System"
echo "  Multi-Language Architecture: Rust + C++ + C"
echo "═══════════════════════════════════════════════════════════"
echo -e "${NC}"

# Check dependencies
echo -e "${YELLOW}Checking dependencies...${NC}"

if ! command -v cargo &> /dev/null; then
    echo -e "${RED}Error: Rust/Cargo not found. Install from https://rustup.rs${NC}"
    exit 1
fi

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: CMake not found. Install CMake 3.20+${NC}"
    exit 1
fi

if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
    echo -e "${RED}Error: C/C++ compiler not found${NC}"
    exit 1
fi

echo -e "${GREEN}✓ All dependencies found${NC}\n"

# Build Rust frontend
echo -e "${BOLD}${BLUE}[1/4] Building Rust Frontend${NC}"
echo -e "${YELLOW}Location: compiler/frontend${NC}"
cd compiler/frontend
cargo build --release 2>&1 | grep -E "(Compiling|Finished|error|warning)" || true
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}Rust frontend build failed${NC}"
    exit 1
fi
cd ../..
echo -e "${GREEN}✓ Rust frontend built successfully${NC}\n"

# Configure CMake
echo -e "${BOLD}${BLUE}[2/4] Configuring CMake${NC}"
echo -e "${YELLOW}Build directory: build/${NC}"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | grep -E "(Configuring|Build files|error|warning)" || true
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}CMake configuration failed${NC}"
    exit 1
fi
cd ..
echo -e "${GREEN}✓ CMake configured${NC}\n"

# Build C++ middle-end and C backend
echo -e "${BOLD}${BLUE}[3/4] Building C++ Middle-end & C Backend${NC}"
echo -e "${YELLOW}Components: semantic_analyzer.cpp, codegen.c${NC}"
cmake --build build --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
if [ $? -ne 0 ]; then
    echo -e "${RED}C++/C build failed${NC}"
    exit 1
fi
echo -e "${GREEN}✓ C++ and C components built${NC}\n"

# Link final executable
echo -e "${BOLD}${BLUE}[4/4] Linking Final Executable${NC}"
echo -e "${YELLOW}Output: build/subc${NC}"
if [ ! -f build/subc ]; then
    echo -e "${RED}Error: Final executable not created${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Executable linked successfully${NC}\n"

# Build summary
echo -e "${BOLD}${GREEN}"
echo "═══════════════════════════════════════════════════════════"
echo "  Build Complete!"
echo "═══════════════════════════════════════════════════════════"
echo -e "${NC}"

echo -e "${BOLD}Architecture Summary:${NC}"
echo -e "  ${BLUE}✓ Rust Frontend${NC}      - Memory-safe lexer/parser"
echo -e "  ${BLUE}✓ C++ Middle-end${NC}     - Advanced optimizations"
echo -e "  ${BLUE}✓ C Backend${NC}          - Portable code generation"
echo ""

echo -e "${BOLD}Executable Location:${NC}"
echo -e "  ${GREEN}./build/subc${NC}"
echo ""

echo -e "${BOLD}Usage:${NC}"
echo -e "  ${YELLOW}./build/subc example.sb web -v${NC}"
echo -e "  ${YELLOW}./build/subc program.sb android -O3${NC}"
echo -e "  ${YELLOW}./build/subc app.sb native --verbose${NC}"
echo ""

echo -e "${BOLD}Performance Features:${NC}"
echo "  • LTO (Link-Time Optimization) enabled"
echo "  • Native CPU optimizations (-march=native)"
echo "  • Fast-math optimizations"
echo "  • Multi-threaded compilation"
echo ""

# Optional: Run quick test
if [ -f example.sb ]; then
    echo -e "${BOLD}Running quick test...${NC}"
    ./build/subc example.sb web -v
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Test compilation successful!${NC}"
    fi
fi

echo -e "\n${BOLD}${GREEN}Ready to compile SUB programs!${NC}\n"
