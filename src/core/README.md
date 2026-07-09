# Core Compiler Components

This directory contains the core components of the SUB language compiler.

## Files

- lexer.c - Tokenization and lexical analysis
- parser_enhanced.c - Recursive-descent parser (produces the AST used by every tool)
- semantic.c - Semantic analysis and symbol table management
- type_system.c / type_system.h - Type system implementation
- interpreter.c / interpreter.h - Tree-walking evaluator used by `subi`
- utils.c - Utility functions for the compiler
