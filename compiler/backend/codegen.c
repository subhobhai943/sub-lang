/**
 * SUB Language Backend - C Implementation
 * Code Generation for Multiple Platforms
 */

#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CodeBuffer {
    char* data;
    size_t size;
    size_t capacity;
} CodeBuffer;

static CodeBuffer* buffer_create(size_t initial_capacity) {
    CodeBuffer* buf = malloc(sizeof(CodeBuffer));
    buf->capacity = initial_capacity;
    buf->size = 0;
    buf->data = malloc(initial_capacity);
    buf->data[0] = '\0';
    return buf;
}

static void buffer_append(CodeBuffer* buf, const char* str) {
    size_t len = strlen(str);
    while (buf->size + len + 1 > buf->capacity) {
        buf->capacity *= 2;
        buf->data = realloc(buf->data, buf->capacity);
    }
    strcat(buf->data, str);
    buf->size += len;
}

static void buffer_free(CodeBuffer* buf) {
    free(buf->data);
    free(buf);
}

char* sub_backend_generate_web(void* ast_root) {
    CodeBuffer* buf = buffer_create(4096);
    
    buffer_append(buf, "// Generated JavaScript\n");
    buffer_append(buf, "'use strict';\n\n");
    buffer_append(buf, "// SUB Runtime\n");
    buffer_append(buf, "const SUB = {\n");
    buffer_append(buf, "  print: (msg) => console.log(msg),\n");
    buffer_append(buf, "  input: (prompt) => window.prompt(prompt)\n");
    buffer_append(buf, "};\n\n");
    buffer_append(buf, "// User code\n");
    
    char* result = strdup(buf->data);
    buffer_free(buf);
    return result;
}

char* sub_backend_generate_android(void* ast_root) {
    CodeBuffer* buf = buffer_create(4096);
    
    buffer_append(buf, "// Generated Java for Android\n");
    buffer_append(buf, "package com.sublang.app;\n\n");
    buffer_append(buf, "import android.app.Activity;\n");
    buffer_append(buf, "import android.os.Bundle;\n\n");
    buffer_append(buf, "public class MainActivity extends Activity {\n");
    buffer_append(buf, "    @Override\n");
    buffer_append(buf, "    protected void onCreate(Bundle savedInstanceState) {\n");
    buffer_append(buf, "        super.onCreate(savedInstanceState);\n");
    buffer_append(buf, "        // User code here\n");
    buffer_append(buf, "    }\n");
    buffer_append(buf, "}\n");
    
    char* result = strdup(buf->data);
    buffer_free(buf);
    return result;
}

char* sub_backend_generate_native(void* ast_root) {
    CodeBuffer* buf = buffer_create(4096);
    
    buffer_append(buf, "// Generated C Code\n");
    buffer_append(buf, "#include <stdio.h>\n");
    buffer_append(buf, "#include <stdlib.h>\n\n");
    buffer_append(buf, "int main(int argc, char** argv) {\n");
    buffer_append(buf, "    // User code\n");
    buffer_append(buf, "    return 0;\n");
    buffer_append(buf, "}\n");
    
    char* result = strdup(buf->data);
    buffer_free(buf);
    return result;
}

void sub_backend_free_code(char* code) {
    if (code) free(code);
}
