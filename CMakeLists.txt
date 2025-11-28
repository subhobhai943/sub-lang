cmake_minimum_required(VERSION 3.20)
project(SUBCompiler VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -O3 -march=native -flto)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ffast-math")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffast-math")
endif()

# Rust frontend library
set(RUST_FRONTEND_DIR ${CMAKE_SOURCE_DIR}/compiler/frontend)
set(RUST_TARGET_DIR ${RUST_FRONTEND_DIR}/target/release)

if(WIN32)
    set(RUST_LIB ${RUST_TARGET_DIR}/sub_frontend.lib)
else()
    set(RUST_LIB ${RUST_TARGET_DIR}/libsub_frontend.a)
endif()

add_custom_command(
    OUTPUT ${RUST_LIB}
    COMMAND cargo build --release
    WORKING_DIRECTORY ${RUST_FRONTEND_DIR}
    COMMENT "Building Rust frontend..."
)

add_custom_target(rust_frontend DEPENDS ${RUST_LIB})

# C++ Middle-end
add_library(sub_middle_end STATIC
    compiler/middle_end/semantic_analyzer.cpp
)
target_include_directories(sub_middle_end PUBLIC
    ${CMAKE_SOURCE_DIR}/compiler/middle_end
)
set_target_properties(sub_middle_end PROPERTIES
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON
)

# C Backend
add_library(sub_backend STATIC
    compiler/backend/codegen.c
)
target_include_directories(sub_backend PUBLIC
    ${CMAKE_SOURCE_DIR}/compiler/backend
)
set_target_properties(sub_backend PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)

# Main compiler executable
add_executable(subc
    compiler/main.c
)

target_link_libraries(subc PRIVATE
    sub_middle_end
    sub_backend
    ${RUST_LIB}
)

if(UNIX AND NOT APPLE)
    target_link_libraries(subc PRIVATE dl pthread m)
elseif(APPLE)
    target_link_libraries(subc PRIVATE pthread)
endif()

add_dependencies(subc rust_frontend)

# Install
install(TARGETS subc DESTINATION bin)

# Tests
enable_testing()
add_subdirectory(tests OPTIONAL)
