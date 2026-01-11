/* ========================================
   SUB Language - Intermediate Representation (IR)
   Platform-independent representation for optimization and code generation
   File: ir.h
   ======================================== */

#ifndef SUB_IR_H
#define SUB_IR_H

#include <stdint.h>
#include <stdbool.h>

/* IR Operation Types */
typedef enum {
    // Arithmetic
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_MOD,
    
    // Comparison
    IR_EQ,
    IR_NE,
    IR_LT,
    IR_LE,
    IR_GT,
    IR_GE,
    
    // Logical
    IR_AND,
    IR_OR,
    IR_NOT,
    
    // Memory
    IR_LOAD,
    IR_STORE,
    IR_ALLOC,
    IR_ALLOC_ARRAY,
    IR_LOAD_ELEM,
    IR_STORE_ELEM,
    
    // Control flow
    IR_LABEL,
    IR_JUMP,
    IR_JUMP_IF,
    IR_JUMP_IF_NOT,
    IR_CALL,
    IR_RETURN,
    
    // Data
    IR_CONST_INT,
    IR_CONST_FLOAT,
    IR_CONST_STRING,
    IR_MOVE,
    
    // Functions
    IR_FUNC_START,
    IR_FUNC_END,
    IR_PARAM,
    
    // Special
    IR_PRINT,
    IR_INPUT,
    IR_CAST,
    IR_PUSH,   // Push generic register/value to stack
    IR_POP,    // Pop to generic register
    IR_PHI,
    IR_NEW,    // Allocate object (new ClassName)
    IR_GET_FIELD, // Get object field (obj.field)
    IR_SET_FIELD, // Set object field (obj.field = value)
    IR_CLASS_DEF  // Define class (metadata)
} IROpcode;

/* IR Value Types */
typedef enum {
    IR_TYPE_VOID,
    IR_TYPE_INT,
    IR_TYPE_FLOAT,
    IR_TYPE_STRING,
    IR_TYPE_BOOL,
    IR_TYPE_POINTER,
    IR_TYPE_LABEL,
    IR_TYPE_OBJECT
} IRType;

typedef enum {
    IR_VAL_CONST,
    IR_VAL_REG,
    IR_VAL_VAR, // Stack variable (pre-reg allocation)
    IR_VAL_LABEL
} IRValueKind;

/* IR Value */
typedef struct IRValue {
    IRType type;
    IRValueKind kind;
    union {
        int64_t int_val;
        double float_val;
        char *string_val;
        int reg_num;      // Virtual register number
        char *label;      // Label name
    } data;
    char *name;           // Optional variable name
} IRValue;

/* IR Instruction */
typedef struct IRInstruction {
    IROpcode opcode;
    IRValue *dest;        // Destination (result)
    IRValue *src1;        // First operand
    IRValue *src2;        // Second operand
    char *comment;        // Optional comment for debugging
    struct IRInstruction *next;
} IRInstruction;

/* Symbol Table Entry */
typedef struct IRSymbol {
    char *name;
    int stack_offset; // Offset from base pointer (RBP)
    IRType type;
    struct IRSymbol *next;
} IRSymbol;

/* Symbol Table (Scope) */
typedef struct IRSymbolTable {
    IRSymbol *head;
    struct IRSymbolTable *parent;
    int current_offset;
} IRSymbolTable;

/* IR Function */
typedef struct IRFunction {
    char *name;
    IRType return_type;
    IRValue **params;     // Array of parameters
    int param_count;
    IRInstruction *instructions;  // Linked list of instructions
    int local_count;      // Number of local variables
    int reg_count;        // Number of virtual registers used
    IRSymbolTable *sym_table; // Function's symbol table
    struct IRFunction *next;
} IRFunction;

/* IR Class field information */
typedef struct IRClassField {
    char *name;
    IRType type;
    int offset;
    struct IRClassField *next;
} IRClassField;

/* IR Class definition */
typedef struct IRClass {
    char *name;
    IRClassField *fields;
    int field_count;
    int size;
    struct IRClass *next;
} IRClass;

/* IR Module (represents entire program) */
typedef struct IRModule {
    IRFunction *functions;
    IRClass *classes;        // Class definitions
    char **string_literals;  // Global string constants
    int string_count;
    char *entry_point;       // Name of main function
} IRModule;

/* Symbol Table Functions */
IRSymbolTable* ir_symbol_table_create(IRSymbolTable *parent);
void ir_symbol_table_free(IRSymbolTable *table);
IRSymbol* ir_symbol_table_add(IRSymbolTable *table, const char *name, IRType type);
IRSymbol* ir_symbol_table_lookup(IRSymbolTable *table, const char *name);

/* IR Builder API */
IRModule* ir_module_create(void);
void ir_module_free(IRModule *module);

IRFunction* ir_function_create(const char *name, IRType return_type, IRSymbolTable *parent_scope);
void ir_function_add_param(IRFunction *func, IRValue *param);
void ir_function_add_instruction(IRFunction *func, IRInstruction *instr);

IRInstruction* ir_instruction_create(IROpcode opcode);
IRValue* ir_value_create_int(int64_t value);
IRValue* ir_value_create_float(double value);
IRValue* ir_value_create_string(const char *value);
IRValue* ir_value_create_reg(int reg_num, IRType type);
IRValue* ir_value_create_label(const char *label);

/* Convert AST to IR */
IRModule* ir_generate_from_ast(void *ast_root);

/* Class management */
IRClass* ir_class_create(const char *name);
void ir_class_add_field(IRClass *cls, const char *field_name, IRType type);
IRClass* ir_class_lookup(IRModule *module, const char *name);
void ir_class_free(IRClass *cls);

/* Optimize IR */
void ir_optimize(IRModule *module);

/* Print IR (for debugging) */
void ir_print(IRModule *module);

#endif /* SUB_IR_H */
