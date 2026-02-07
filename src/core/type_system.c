/* ========================================
   SUB Language - Type System Implementation
   Provides type inference, validation, and target language mapping
   File: type_system.c
   ======================================== */

#define _GNU_SOURCE
#include "type_system.h"
#include "sub_compiler.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ========================================
   Global Type Mapping Table
   ======================================== */

const TypeMapping TYPE_MAPPINGS[] = {
    /* SUB Type        Python    JavaScript  TypeScript   Java        Ruby      C++            C             Rust        Swift         Kotlin       Go */
    {SUB_TYPE_VOID,    "None",   "void",     "void",      "void",     "nil",    "void",        "void",       "()",       "Void",       "Unit",      "void"},
    {SUB_TYPE_INT,     "int",    "number",   "number",    "int",      "Integer","int",         "int",        "i64",      "Int",        "Int",       "int"},
    {SUB_TYPE_FLOAT,   "float",  "number",   "number",    "double",   "Float",  "double",      "double",     "f64",      "Double",     "Double",    "float64"},
    {SUB_TYPE_STRING,  "str",    "string",   "string",    "String",   "String", "std::string", "char*",      "String",   "String",     "String",    "string"},
    {SUB_TYPE_BOOL,    "bool",   "boolean",  "boolean",   "boolean",  "Boolean","bool",        "bool",       "bool",     "Bool",       "Boolean",   "bool"},
    {SUB_TYPE_ARRAY,   "list",   "Array",    "any[]",     "ArrayList","Array",  "std::vector", "array",      "Vec",      "Array",      "List",      "[]interface{}"},
    {SUB_TYPE_OBJECT,  "dict",   "object",   "Record<string, any>", "Object", "Hash", "std::map", "struct", "HashMap", "Dictionary", "Map", "map[string]interface{}"},
    {SUB_TYPE_FUNCTION,"Callable","Function","(...args: any[]) => any","Object","Proc","std::function","void*", "fn()", "((Any) -> Any)", "(Any) -> Any", "func(...interface{}) interface{}"},
    {SUB_TYPE_NULL,    "None",   "null",     "null",      "null",     "nil",    "nullptr",     "NULL",       "None",     "nil",        "null",      "nil"},
    {SUB_TYPE_ANY,     "Any",    "any",      "any",       "Object",   "Object", "auto",        "void*",      "dyn Any",  "Any",        "Any",       "interface{}"},
};

const size_t TYPE_MAPPINGS_COUNT = sizeof(TYPE_MAPPINGS) / sizeof(TYPE_MAPPINGS[0]);

/* ========================================
   Type Info Creation/Destruction
   ======================================== */

TypeInfo* type_info_create(SubType base_type) {
    TypeInfo *info = calloc(1, sizeof(TypeInfo));
    if (!info) return NULL;
    info->base_type = base_type;
    info->is_const = false;
    info->is_nullable = false;
    return info;
}

TypeInfo* type_info_create_array(TypeInfo *element_type) {
    TypeInfo *info = type_info_create(SUB_TYPE_ARRAY);
    if (!info) return NULL;
    info->element_type = element_type;
    return info;
}

TypeInfo* type_info_create_function(TypeInfo *return_type, TypeInfo **params, int param_count) {
    TypeInfo *info = type_info_create(SUB_TYPE_FUNCTION);
    if (!info) return NULL;
    info->return_type = return_type;
    info->param_types = params;
    info->param_count = param_count;
    return info;
}

void type_info_free(TypeInfo *info) {
    if (!info) return;
    if (info->element_type) type_info_free(info->element_type);
    if (info->return_type) type_info_free(info->return_type);
    if (info->param_types) {
        for (int i = 0; i < info->param_count; i++) {
            type_info_free(info->param_types[i]);
        }
        free(info->param_types);
    }
    free(info);
}

TypeInfo* type_info_copy(const TypeInfo *info) {
    if (!info) return NULL;
    TypeInfo *copy = type_info_create(info->base_type);
    if (!copy) return NULL;
    
    copy->is_const = info->is_const;
    copy->is_nullable = info->is_nullable;
    
    if (info->element_type) {
        copy->element_type = type_info_copy(info->element_type);
    }
    if (info->return_type) {
        copy->return_type = type_info_copy(info->return_type);
    }
    if (info->param_types && info->param_count > 0) {
        copy->param_types = malloc(sizeof(TypeInfo*) * info->param_count);
        copy->param_count = info->param_count;
        for (int i = 0; i < info->param_count; i++) {
            copy->param_types[i] = type_info_copy(info->param_types[i]);
        }
    }
    return copy;
}

/* ========================================
   Type Inference
   ======================================== */

SubType type_infer_from_literal(const char *value) {
    if (!value || !*value) return SUB_TYPE_NULL;
    
    // String literal (starts with quote)
    if (value[0] == '"' || value[0] == '\'') {
        return SUB_TYPE_STRING;
    }
    
    // Boolean
    if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
        return SUB_TYPE_BOOL;
    }
    
    // Null
    if (strcmp(value, "null") == 0 || strcmp(value, "nil") == 0 || strcmp(value, "None") == 0) {
        return SUB_TYPE_NULL;
    }
    
    // Check if numeric
    const char *p = value;
    bool has_dot = false;
    bool has_digit = false;
    
    if (*p == '-' || *p == '+') p++;
    
    while (*p) {
        if (isdigit(*p)) {
            has_digit = true;
        } else if (*p == '.' && !has_dot) {
            has_dot = true;
        } else {
            // Not a simple number
            return SUB_TYPE_UNKNOWN;
        }
        p++;
    }
    
    if (has_digit) {
        return has_dot ? SUB_TYPE_FLOAT : SUB_TYPE_INT;
    }
    
    return SUB_TYPE_UNKNOWN;
}

TypeInfo* type_infer_from_node(ASTNode *node) {
    if (!node) return type_info_create(SUB_TYPE_UNKNOWN);
    
    switch (node->type) {
        case AST_LITERAL:
            return type_info_create(type_infer_from_literal(node->value));
            
        case AST_IDENTIFIER:
            // Would need symbol table lookup for proper inference
            return type_info_create(SUB_TYPE_ANY);
            
        case AST_BINARY_EXPR: {
            TypeInfo *left = type_infer_from_node(node->left);
            TypeInfo *right = type_infer_from_node(node->right);
            TypeInfo *result = NULL;
            
            if (type_validate_binary_op(left, node->value, right, &result)) {
                type_info_free(left);
                type_info_free(right);
                return result;
            }
            
            type_info_free(left);
            type_info_free(right);
            return type_info_create(SUB_TYPE_UNKNOWN);
        }
        
        case AST_CALL_EXPR:
            // Function return type would need symbol table
            return type_info_create(SUB_TYPE_ANY);
            
        case AST_ARRAY_LITERAL:
            return type_info_create(SUB_TYPE_ARRAY);
            
        case AST_OBJECT_LITERAL:
            return type_info_create(SUB_TYPE_OBJECT);
            
        default:
            return type_info_create(SUB_TYPE_UNKNOWN);
    }
}

TypeInfo* type_infer_expression(ASTNode *expr) {
    return type_infer_from_node(expr);
}

/* ========================================
   Type Mapping
   ======================================== */

const char* type_map_to(SubType type, TargetLanguage target) {
    for (size_t i = 0; i < TYPE_MAPPINGS_COUNT; i++) {
        if (TYPE_MAPPINGS[i].sub_type == type) {
            switch (target) {
                case TARGET_PYTHON:     return TYPE_MAPPINGS[i].python_type;
                case TARGET_JAVASCRIPT: return TYPE_MAPPINGS[i].javascript_type;
                case TARGET_TYPESCRIPT: return TYPE_MAPPINGS[i].typescript_type;
                case TARGET_JAVA:       return TYPE_MAPPINGS[i].java_type;
                case TARGET_RUBY:       return TYPE_MAPPINGS[i].ruby_type;
                case TARGET_CPP:        return TYPE_MAPPINGS[i].cpp_type;
                case TARGET_C:          return TYPE_MAPPINGS[i].c_type;
                case TARGET_RUST:       return TYPE_MAPPINGS[i].rust_type;
                case TARGET_SWIFT:      return TYPE_MAPPINGS[i].swift_type;
                case TARGET_KOTLIN:     return TYPE_MAPPINGS[i].kotlin_type;
                case TARGET_GO:         return TYPE_MAPPINGS[i].go_type;
            }
        }
    }
    
    // Unknown type fallback
    switch (target) {
        case TARGET_PYTHON:     return "object";
        case TARGET_JAVASCRIPT: return "any";
        case TARGET_TYPESCRIPT: return "any";
        case TARGET_JAVA:       return "Object";
        case TARGET_RUBY:       return "Object";
        case TARGET_CPP:        return "auto";
        case TARGET_C:          return "void*";
        case TARGET_RUST:       return "dyn Any";
        case TARGET_SWIFT:      return "Any";
        case TARGET_KOTLIN:     return "Any";
        case TARGET_GO:         return "interface{}";
    }
    return "unknown";
}

const char* type_info_map_to(TypeInfo *info, TargetLanguage target) {
    if (!info) return type_map_to(SUB_TYPE_UNKNOWN, target);
    
    // For simple types, use the base mapping
    if (info->base_type != SUB_TYPE_ARRAY && info->base_type != SUB_TYPE_FUNCTION) {
        return type_map_to(info->base_type, target);
    }
    
    // Arrays need element type (simplified - just returns base array type)
    if (info->base_type == SUB_TYPE_ARRAY) {
        return type_map_to(SUB_TYPE_ARRAY, target);
    }
    
    return type_map_to(info->base_type, target);
}

/* ========================================
   Type Validation
   ======================================== */

bool types_are_equal(TypeInfo *a, TypeInfo *b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    
    if (a->base_type != b->base_type) return false;
    
    // For arrays, check element types
    if (a->base_type == SUB_TYPE_ARRAY) {
        return types_are_equal(a->element_type, b->element_type);
    }
    
    // For functions, check return type and parameters
    if (a->base_type == SUB_TYPE_FUNCTION) {
        if (!types_are_equal(a->return_type, b->return_type)) return false;
        if (a->param_count != b->param_count) return false;
        for (int i = 0; i < a->param_count; i++) {
            if (!types_are_equal(a->param_types[i], b->param_types[i])) return false;
        }
    }
    
    return true;
}

bool types_are_compatible(TypeInfo *a, TypeInfo *b) {
    if (!a || !b) return false;
    
    // Same types are compatible
    if (types_are_equal(a, b)) return true;
    
    // Any is compatible with everything
    if (a->base_type == SUB_TYPE_ANY || b->base_type == SUB_TYPE_ANY) return true;
    
    // Numeric types are compatible with each other
    if (type_is_numeric(a->base_type) && type_is_numeric(b->base_type)) return true;
    
    // Null is compatible with nullable types
    if (a->base_type == SUB_TYPE_NULL && b->is_nullable) return true;
    if (b->base_type == SUB_TYPE_NULL && a->is_nullable) return true;
    
    return false;
}

bool type_validate_binary_op(TypeInfo *left, const char *op, TypeInfo *right, TypeInfo **result_type) {
    if (!left || !right || !op) return false;
    
    // Arithmetic operators
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || 
        strcmp(op, "*") == 0 || strcmp(op, "/") == 0) {
        
        // String concatenation with +
        if (strcmp(op, "+") == 0 && 
            (left->base_type == SUB_TYPE_STRING || right->base_type == SUB_TYPE_STRING)) {
            if (result_type) *result_type = type_info_create(SUB_TYPE_STRING);
            return true;
        }
        
        // Numeric arithmetic
        if (type_is_numeric(left->base_type) && type_is_numeric(right->base_type)) {
            SubType result = (left->base_type == SUB_TYPE_FLOAT || right->base_type == SUB_TYPE_FLOAT)
                           ? SUB_TYPE_FLOAT : SUB_TYPE_INT;
            if (result_type) *result_type = type_info_create(result);
            return true;
        }
        
        // Dynamic types
        if (left->base_type == SUB_TYPE_ANY || right->base_type == SUB_TYPE_ANY) {
            if (result_type) *result_type = type_info_create(SUB_TYPE_ANY);
            return true;
        }
        
        return false;
    }
    
    // Comparison operators
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
        if (result_type) *result_type = type_info_create(SUB_TYPE_BOOL);
        return types_are_compatible(left, right);
    }
    
    // Logical operators
    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 || 
        strcmp(op, "and") == 0 || strcmp(op, "or") == 0) {
        if (result_type) *result_type = type_info_create(SUB_TYPE_BOOL);
        return left->base_type == SUB_TYPE_BOOL && right->base_type == SUB_TYPE_BOOL;
    }
    
    return false;
}

bool type_validate_assignment(TypeInfo *target, TypeInfo *value) {
    return types_are_compatible(target, value);
}

bool type_validate_function_call(TypeInfo *func, TypeInfo **args, int arg_count) {
    if (!func || func->base_type != SUB_TYPE_FUNCTION) return false;
    if (func->param_count != arg_count) return false;
    
    for (int i = 0; i < arg_count; i++) {
        if (!types_are_compatible(func->param_types[i], args[i])) {
            return false;
        }
    }
    
    return true;
}

/* ========================================
   Type Coercion
   ======================================== */

bool type_can_coerce(SubType from, SubType to) {
    if (from == to) return true;
    if (to == SUB_TYPE_ANY) return true;
    
    // Int can coerce to float
    if (from == SUB_TYPE_INT && to == SUB_TYPE_FLOAT) return true;
    
    // Everything can coerce to string
    if (to == SUB_TYPE_STRING) return true;
    
    return false;
}

const char* type_get_coercion(SubType from, SubType to, TargetLanguage target) {
    if (from == to) return "";
    
    // Int to float coercion
    if (from == SUB_TYPE_INT && to == SUB_TYPE_FLOAT) {
        switch (target) {
            case TARGET_PYTHON:     return "float(%s)";
            case TARGET_JAVASCRIPT: return "Number(%s)";
            case TARGET_JAVA:       return "(double)%s";
            case TARGET_CPP:        return "static_cast<double>(%s)";
            case TARGET_C:          return "(double)%s";
            case TARGET_RUST:       return "%s as f64";
            case TARGET_SWIFT:      return "Double(%s)";
            case TARGET_KOTLIN:     return "%s.toDouble()";
            default:                return "%s";
        }
    }
    
    // To string coercion
    if (to == SUB_TYPE_STRING) {
        switch (target) {
            case TARGET_PYTHON:     return "str(%s)";
            case TARGET_JAVASCRIPT: return "String(%s)";
            case TARGET_JAVA:       return "String.valueOf(%s)";
            case TARGET_RUBY:       return "%s.to_s";
            case TARGET_CPP:        return "std::to_string(%s)";
            case TARGET_RUST:       return "%s.to_string()";
            case TARGET_SWIFT:      return "String(%s)";
            case TARGET_KOTLIN:     return "%s.toString()";
            default:                return "%s";
        }
    }
    
    return "%s";
}

/* ========================================
   Default Values
   ======================================== */

const char* type_get_default_value(SubType type, TargetLanguage target) {
    switch (type) {
        case SUB_TYPE_INT:
            return "0";
        case SUB_TYPE_FLOAT:
            return "0.0";
        case SUB_TYPE_STRING:
            switch (target) {
                case TARGET_C: return "\"\"";
                default:       return "\"\"";
            }
        case SUB_TYPE_BOOL:
            switch (target) {
                case TARGET_PYTHON: return "False";
                case TARGET_RUBY:   return "false";
                default:            return "false";
            }
        case SUB_TYPE_ARRAY:
            switch (target) {
                case TARGET_PYTHON:     return "[]";
                case TARGET_JAVASCRIPT: return "[]";
                case TARGET_JAVA:       return "new ArrayList<>()";
                case TARGET_RUBY:       return "[]";
                case TARGET_CPP:        return "{}";
                case TARGET_RUST:       return "vec![]";
                case TARGET_SWIFT:      return "[]";
                case TARGET_KOTLIN:     return "listOf()";
                default:                return "[]";
            }
        case SUB_TYPE_OBJECT:
            switch (target) {
                case TARGET_PYTHON:     return "{}";
                case TARGET_JAVASCRIPT: return "{}";
                case TARGET_JAVA:       return "new HashMap<>()";
                case TARGET_RUBY:       return "{}";
                case TARGET_CPP:        return "{}";
                case TARGET_RUST:       return "HashMap::new()";
                case TARGET_SWIFT:      return "[:]";
                case TARGET_KOTLIN:     return "mapOf()";
                default:                return "{}";
            }
        case SUB_TYPE_NULL:
        case SUB_TYPE_VOID:
            switch (target) {
                case TARGET_PYTHON: return "None";
                case TARGET_RUBY:   return "nil";
                case TARGET_SWIFT:  return "nil";
                case TARGET_CPP:    return "nullptr";
                case TARGET_C:      return "NULL";
                case TARGET_RUST:   return "None";
                default:            return "null";
            }
        default:
            switch (target) {
                case TARGET_PYTHON: return "None";
                case TARGET_RUBY:   return "nil";
                default:            return "null";
            }
    }
}

/* ========================================
   Utility Functions
   ======================================== */

const char* type_to_string(SubType type) {
    switch (type) {
        case SUB_TYPE_UNKNOWN:  return "unknown";
        case SUB_TYPE_VOID:     return "void";
        case SUB_TYPE_INT:      return "int";
        case SUB_TYPE_FLOAT:    return "float";
        case SUB_TYPE_STRING:   return "string";
        case SUB_TYPE_BOOL:     return "bool";
        case SUB_TYPE_ARRAY:    return "array";
        case SUB_TYPE_OBJECT:   return "object";
        case SUB_TYPE_FUNCTION: return "function";
        case SUB_TYPE_NULL:     return "null";
        case SUB_TYPE_AUTO:     return "auto";
        case SUB_TYPE_ANY:      return "any";
        default:                return "unknown";
    }
}

SubType type_from_string(const char *str) {
    if (!str) return SUB_TYPE_UNKNOWN;
    
    if (strcmp(str, "int") == 0 || strcmp(str, "integer") == 0) return SUB_TYPE_INT;
    if (strcmp(str, "float") == 0 || strcmp(str, "double") == 0) return SUB_TYPE_FLOAT;
    if (strcmp(str, "string") == 0 || strcmp(str, "str") == 0) return SUB_TYPE_STRING;
    if (strcmp(str, "bool") == 0 || strcmp(str, "boolean") == 0) return SUB_TYPE_BOOL;
    if (strcmp(str, "array") == 0 || strcmp(str, "list") == 0) return SUB_TYPE_ARRAY;
    if (strcmp(str, "object") == 0 || strcmp(str, "dict") == 0) return SUB_TYPE_OBJECT;
    if (strcmp(str, "function") == 0 || strcmp(str, "func") == 0) return SUB_TYPE_FUNCTION;
    if (strcmp(str, "null") == 0 || strcmp(str, "nil") == 0) return SUB_TYPE_NULL;
    if (strcmp(str, "void") == 0) return SUB_TYPE_VOID;
    if (strcmp(str, "auto") == 0) return SUB_TYPE_AUTO;
    if (strcmp(str, "any") == 0) return SUB_TYPE_ANY;
    
    return SUB_TYPE_UNKNOWN;
}

bool type_is_numeric(SubType type) {
    return type == SUB_TYPE_INT || type == SUB_TYPE_FLOAT;
}

bool type_is_primitive(SubType type) {
    return type == SUB_TYPE_INT || type == SUB_TYPE_FLOAT || 
           type == SUB_TYPE_STRING || type == SUB_TYPE_BOOL;
}

bool type_requires_gc(SubType type) {
    return type == SUB_TYPE_STRING || type == SUB_TYPE_ARRAY || 
           type == SUB_TYPE_OBJECT;
}

/* ========================================
   Memory Model Helpers
   ======================================== */

MemoryModel target_memory_model(TargetLanguage target) {
    switch (target) {
        case TARGET_PYTHON:
        case TARGET_JAVASCRIPT:
        case TARGET_TYPESCRIPT:
        case TARGET_JAVA:
        case TARGET_RUBY:
        case TARGET_KOTLIN:
        case TARGET_GO:
            return MEM_MODEL_GC;
        
        case TARGET_CPP:
        case TARGET_RUST:
        case TARGET_SWIFT:  // ARC
            return MEM_MODEL_RAII;
        
        case TARGET_C:
            return MEM_MODEL_MANUAL;
        
        default:
            return MEM_MODEL_GC;
    }
}

bool target_is_statically_typed(TargetLanguage target) {
    switch (target) {
        case TARGET_JAVA:
        case TARGET_CPP:
        case TARGET_C:
        case TARGET_RUST:
        case TARGET_SWIFT:
        case TARGET_KOTLIN:
        case TARGET_GO:
        case TARGET_TYPESCRIPT:
            return true;
        
        case TARGET_PYTHON:
        case TARGET_JAVASCRIPT:
        case TARGET_RUBY:
            return false;
        
        default:
            return false;
    }
}

bool target_requires_type_annotations(TargetLanguage target) {
    switch (target) {
        case TARGET_JAVA:
        case TARGET_C:
        case TARGET_RUST:  // Usually yes, though inference exists
        case TARGET_SWIFT:
        case TARGET_KOTLIN:
        case TARGET_GO:
            return true;
        
        case TARGET_CPP:       // auto keyword available
        case TARGET_PYTHON:    // Optional type hints
        case TARGET_JAVASCRIPT:
        case TARGET_TYPESCRIPT:// Required but uses different system
        case TARGET_RUBY:
            return false;
        
        default:
            return false;
    }
}
