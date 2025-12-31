/* ========================================
   SUB Language - Type System
   Provides type inference, validation, and target language mapping
   File: type_system.h
   ======================================== */

#ifndef TYPE_SYSTEM_H
#define TYPE_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>

/* ========================================
   Target Language Enumeration
   ======================================== */

typedef enum {
    TARGET_PYTHON,
    TARGET_JAVASCRIPT,
    TARGET_TYPESCRIPT,
    TARGET_JAVA,
    TARGET_RUBY,
    TARGET_CPP,
    TARGET_C,
    TARGET_RUST,
    TARGET_SWIFT,
    TARGET_KOTLIN,
    TARGET_GO
} TargetLanguage;

/* ========================================
   SUB Type Enumeration
   ======================================== */

typedef enum {
    SUB_TYPE_UNKNOWN,
    SUB_TYPE_VOID,
    SUB_TYPE_INT,
    SUB_TYPE_FLOAT,
    SUB_TYPE_STRING,
    SUB_TYPE_BOOL,
    SUB_TYPE_ARRAY,
    SUB_TYPE_OBJECT,
    SUB_TYPE_FUNCTION,
    SUB_TYPE_NULL,
    SUB_TYPE_AUTO,       // Type to be inferred
    SUB_TYPE_ANY         // Dynamic type (for Python/JS/Ruby)
} SubType;

/* ========================================
   Type Information Structure
   ======================================== */

typedef struct TypeInfo {
    SubType base_type;
    struct TypeInfo *element_type;  // For arrays: type of elements
    struct TypeInfo *return_type;   // For functions: return type
    struct TypeInfo **param_types;  // For functions: parameter types
    int param_count;
    bool is_const;
    bool is_nullable;
} TypeInfo;

/* ========================================
   Type Mapping Tables
   ======================================== */

typedef struct {
    SubType sub_type;
    const char *python_type;
    const char *javascript_type;
    const char *java_type;
    const char *ruby_type;
    const char *cpp_type;
    const char *c_type;
    const char *rust_type;
    const char *swift_type;
    const char *kotlin_type;
} TypeMapping;

/* Global type mapping table */
extern const TypeMapping TYPE_MAPPINGS[];
extern const size_t TYPE_MAPPINGS_COUNT;

/* ========================================
   Type System Functions
   ======================================== */

/* Creation and destruction */
TypeInfo* type_info_create(SubType base_type);
TypeInfo* type_info_create_array(TypeInfo *element_type);
TypeInfo* type_info_create_function(TypeInfo *return_type, TypeInfo **params, int param_count);
void type_info_free(TypeInfo *info);
TypeInfo* type_info_copy(const TypeInfo *info);

/* Type inference from AST */
struct ASTNode;  // Forward declaration
SubType type_infer_from_literal(const char *value);
TypeInfo* type_infer_from_node(struct ASTNode *node);
TypeInfo* type_infer_expression(struct ASTNode *expr);

/* Type mapping to target languages */
const char* type_map_to(SubType type, TargetLanguage target);
const char* type_info_map_to(TypeInfo *info, TargetLanguage target);

/* Type validation */
bool types_are_equal(TypeInfo *a, TypeInfo *b);
bool types_are_compatible(TypeInfo *a, TypeInfo *b);
bool type_validate_binary_op(TypeInfo *left, const char *op, TypeInfo *right, TypeInfo **result_type);
bool type_validate_assignment(TypeInfo *target, TypeInfo *value);
bool type_validate_function_call(TypeInfo *func, TypeInfo **args, int arg_count);

/* Type coercion */
bool type_can_coerce(SubType from, SubType to);
const char* type_get_coercion(SubType from, SubType to, TargetLanguage target);

/* Default values for types in each language */
const char* type_get_default_value(SubType type, TargetLanguage target);

/* Utility functions */
const char* type_to_string(SubType type);
SubType type_from_string(const char *str);
bool type_is_numeric(SubType type);
bool type_is_primitive(SubType type);
bool type_requires_gc(SubType type);

/* Memory model helpers */
typedef enum {
    MEM_MODEL_GC,        // Garbage collected (Python, JS, Java, Ruby, Kotlin)
    MEM_MODEL_RAII,      // RAII / destructors (C++, Rust)
    MEM_MODEL_MANUAL     // Manual memory management (C)
} MemoryModel;

MemoryModel target_memory_model(TargetLanguage target);
bool target_is_statically_typed(TargetLanguage target);
bool target_requires_type_annotations(TargetLanguage target);

#endif /* TYPE_SYSTEM_H */
