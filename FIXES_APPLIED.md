# SUB Compiler Fixes Applied

## Problem 1: Enum Name Conflicts in codegen_x64.h

### Issue
The x86-64 code generator had enum names that conflicted with system headers:
- `REG_RAX`, `REG_RBX`, `REG_RCX`, etc. were already defined in `<sys/ucontext.h>`
- This caused compilation errors: "redeclaration of enumerator 'REG_R8'"

### Solution
Renamed all register enum constants in `codegen_x64.h`:
- Changed `REG_RAX` → `X64_REG_RAX`
- Changed `REG_RBX` → `X64_REG_RBX`
- Changed all 16 register names similarly
- Changed `REG_COUNT` → `X64_REG_COUNT`

Updated all references in `codegen_x64.c`:
- Line 37-38: `ctx->reg_in_use[X64_REG_RSP]`, `ctx->reg_in_use[X64_REG_RBP]`
- Line 49: `if (reg >= X64_REG_COUNT)`
- Line 56-57: Updated priority array with `X64_REG_*` names
- Line 67: Changed loop `for (int i = X64_REG_RBX; i < X64_REG_COUNT; i++)`
- Line 74: Changed return value to `X64_REG_RAX`
- Line 78: Changed conditions to use `X64_REG_RSP` and `X64_REG_RBP`

### Files Modified
- `sub-lang/codegen_x64.h` (enum definition)
- `sub-lang/codegen_x64.c` (all usages)

---

## Problem 2: Duplicate Function Definition

### Issue
`sub_native_compiler.c` had a `read_file()` function, but `sub_compiler.h` already declared one.
- Compilation error: "static declaration of 'read_file' follows non-static declaration"

### Solution
Renamed the function in `sub_native_compiler.c`:
- Changed function name: `read_file()` → `read_file_native()`
- Updated all call sites to use `read_file_native(input_file)` instead of `read_file(input_file)`

### Files Modified
- `sub-lang/sub_native_compiler.c` (function definition and calls)

---

## Problem 3: AST Node Type Mismatches in ir.c

### Issue
The ir.c file referenced incorrect AST node type names that don't exist in the actual AST definition:
- Used `NODE_PROGRAM` but actual enum is `AST_PROGRAM`
- Used `NODE_VAR_DECL` but actual enum is `AST_VAR_DECL`
- Used `NODE_FUNCTION_CALL` but actual enum is `AST_CALL_EXPR`
- Used `NODE_BINARY_OP` but actual enum is `AST_BINARY_EXPR`
- Used `NODE_NUMBER` but actual enum is `AST_LITERAL`

Additionally, code referenced `node->name` which doesn't exist in the ASTNode struct:
- Actual field is `node->value`

Compilation errors:
```
ir.c:206:28: error: 'ASTNode' has no member named 'name'
ir.c:218:14: error: 'NODE_NUMBER' undeclared (first use in this function)
```

### Solution
Fixed all switch cases in `ir_generate_from_ast_node()` function (lines 161-233):

**Case conversions:**
```c
// OLD → NEW
case NODE_PROGRAM → case AST_PROGRAM
case NODE_VAR_DECL → case AST_VAR_DECL
case NODE_FUNCTION_CALL → case AST_CALL_EXPR
case NODE_BINARY_OP → case AST_BINARY_EXPR
case NODE_NUMBER → case AST_LITERAL
```

**Field access fixes:**
- Changed `node->name` → `node->value` throughout
- Line 176: `alloc->dest->name = node->value ? strdup(node->value) : NULL;`
- Line 191: `if (node->value && strcmp(node->value, "print") == 0)`
- Line 204-205: Changed to use `node->left` and `node->right` instead of `node->children[0/1]`
- Line 209: `if (strcmp(node->value, "+") == 0) op = IR_ADD;`
- Line 222: `load_const->src1 = ir_value_create_int(atoi(node->value));`

### Files Modified
- `sub-lang/ir.c` (switch statement and all node references)

---

## How to Apply These Fixes

### Step 1: Fix codegen_x64.h
In `codegen_x64.h`, change the enum definition:
```c
typedef enum {
    X64_REG_RAX = 0,  // was REG_RAX
    X64_REG_RBX,      // was REG_RBX
    X64_REG_RCX,      // was REG_RCX
    X64_REG_RDX,      // was REG_RDX
    X64_REG_RSI,      // was REG_RSI
    X64_REG_RDI,      // was REG_RDI
    X64_REG_RBP,      // was REG_RBP
    X64_REG_RSP,      // was REG_RSP
    X64_REG_R8,       // was REG_R8
    X64_REG_R9,       // was REG_R9
    X64_REG_R10,      // was REG_R10
    X64_REG_R11,      // was REG_R11
    X64_REG_R12,      // was REG_R12
    X64_REG_R13,      // was REG_R13
    X64_REG_R14,      // was REG_R14
    X64_REG_R15,      // was REG_R15
    X64_REG_COUNT
} X64Register;
```

Also update the struct:
```c
bool reg_in_use[X64_REG_COUNT]; // was REG_COUNT
```

### Step 2: Fix codegen_x64.c
Replace all `REG_*` with `X64_REG_*`:
- `reg_in_use[REG_RSP]` → `reg_in_use[X64_REG_RSP]`
- `reg_in_use[REG_RBP]` → `reg_in_use[X64_REG_RBP]`
- Priority array in `x64_alloc_register()`: all 9 registers need `X64_` prefix
- Loop condition: `REG_RBX` → `X64_REG_RBX` and `REG_COUNT` → `X64_REG_COUNT`
- Comparisons: `reg != REG_RSP` → `reg != X64_REG_RSP`, etc.

### Step 3: Fix sub_native_compiler.c
1. Rename function (line ~28):
   ```c
   // OLD: static char* read_file(const char *filename)
   // NEW:
   static char* read_file_native(const char *filename)
   ```

2. Update call site (line ~68):
   ```c
   // OLD: char *source = read_file(input_file);
   // NEW:
   char *source = read_file_native(input_file);
   ```

### Step 4: Fix ir.c
In `ir_generate_from_ast_node()` function, update switch statement:

```c
// Line 165: AST_PROGRAM (was NODE_PROGRAM)
case AST_PROGRAM:
    for (int i = 0; i < node->child_count; i++) {
        ir_generate_from_ast_node(func, node->children[i]);
    }
    break;

// Line 172: AST_VAR_DECL (was NODE_VAR_DECL)
case AST_VAR_DECL: {
    IRInstruction *alloc = ir_instruction_create(IR_ALLOC);
    alloc->dest = ir_value_create_reg(func->local_count++, IR_TYPE_INT);
    alloc->dest->name = node->value ? strdup(node->value) : NULL;  // was node->name
    ir_function_add_instruction(func, alloc);
    
    if (node->child_count > 0) {
        ir_generate_from_ast_node(func, node->children[0]);
        IRInstruction *store = ir_instruction_create(IR_STORE);
        store->dest = alloc->dest;
        ir_function_add_instruction(func, store);
    }
    break;
}

// Line 190: AST_CALL_EXPR (was NODE_FUNCTION_CALL)
case AST_CALL_EXPR:
    if (node->value && strcmp(node->value, "print") == 0) {  // was node->name
        for (int i = 0; i < node->child_count; i++) {
            ir_generate_from_ast_node(func, node->children[i]);
        }
        IRInstruction *print = ir_instruction_create(IR_PRINT);
        ir_function_add_instruction(func, print);
    }
    break;

// Line 202: AST_BINARY_EXPR (was NODE_BINARY_OP)
case AST_BINARY_EXPR: {
    if (node->left) ir_generate_from_ast_node(func, node->left);
    if (node->right) ir_generate_from_ast_node(func, node->right);
    
    IROpcode op = IR_ADD;
    if (node->value) {  // was node->name
        if (strcmp(node->value, "+") == 0) op = IR_ADD;
        else if (strcmp(node->value, "-") == 0) op = IR_SUB;
        else if (strcmp(node->value, "*") == 0) op = IR_MUL;
        else if (strcmp(node->value, "/") == 0) op = IR_DIV;
    }
    
    IRInstruction *bin_op = ir_instruction_create(op);
    bin_op->dest = ir_value_create_reg(func->reg_count++, IR_TYPE_INT);
    ir_function_add_instruction(func, bin_op);
    break;
}

// Line 222: AST_LITERAL (was NODE_NUMBER)
case AST_LITERAL: {
    IRInstruction *load_const = ir_instruction_create(IR_CONST_INT);
    load_const->dest = ir_value_create_reg(func->reg_count++, IR_TYPE_INT);
    if (node->value) {  // was node->name
        load_const->src1 = ir_value_create_int(atoi(node->value));
    } else {
        load_const->src1 = ir_value_create_int(0);
    }
    ir_function_add_instruction(func, load_const);
    break;
}
```

---

## Verification

After applying all fixes, run:
```bash
cd sub-lang
make clean && make
```

Expected output:
```
✅ Native compiler ready!
✅ Transpiler ready!
✅ Build complete!
```

Both `subc-native` and `sublang` should be compiled successfully.

---

## Summary

| Problem | Root Cause | Fix |
|---------|-----------|-----|
| Enum conflicts | `REG_*` names in system headers | Prefix all with `X64_` |
| Duplicate function | Name collision with header | Rename to `read_file_native()` |
| Wrong AST types | Outdated node type names in switch | Update to actual enum names (`AST_*`) |
| Wrong field access | Using `node->name` instead of `node->value` | Replace all `->name` with `->value` |

All three issues were blocking the native compiler compilation. After fixes, both compilers build successfully.
