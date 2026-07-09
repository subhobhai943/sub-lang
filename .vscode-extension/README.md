# SUB Language Support for VS Code

[![Version](https://img.shields.io/badge/version-1.1.0-blue.svg)](https://github.com/subhobhai943/sub-lang)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](https://github.com/subhobhai943/sub-lang/blob/main/LICENSE)

Official VS Code extension providing syntax highlighting and language support for the SUB Programming Language.

## Features

### ✨ Syntax Highlighting

- **Keywords**: `var`, `function`, `if`, `for`, `while`, `end`, etc.
- **Strings**: Double and single-quoted strings with escape sequences
- **Numbers**: Integers, floats, hex, binary, and octal
- **Comments**: Line comments starting with `#`
- **Operators**: Arithmetic, comparison, logical, and assignment
- **Functions**: Function declarations and calls
- **Variables**: Variable declarations and usage
- **Embedded Code**: Syntax highlighting for embedded Python, JavaScript, Java, C, Ruby, and more

### 📝 Code Snippets

Quick snippets for common SUB constructs:

- `var` - Variable declaration
- `func` - Function declaration
- `if` - If statement
- `ifelse` - If-else statement
- `ifelif` - If-elif-else statement
- `for` - For loop
- `while` - While loop
- `print` - Print statement
- `input` - Input statement
- `class` - Class declaration
- `import` - Import statement
- `embedpy` - Embed Python code
- `embedjs` - Embed JavaScript code
- `return` - Return statement
- `break` - Break statement
- `continue` - Continue statement

### 🛠️ Editor Features

- **Auto-closing pairs**: Brackets, quotes, and parentheses
- **Bracket matching**: Matching pairs are highlighted
- **Code folding**: Fold functions, loops, and conditionals
- **Smart indentation**: Auto-indent based on SUB syntax
- **Comment toggling**: Toggle line comments with `Ctrl+/`

## Supported File Extensions

- `.sb` - Standard SUB files
- `.sub` - Alternative SUB files

## Installation

### From VSIX (Manual Installation)

1. Download the latest `.vsix` file from [releases](https://github.com/subhobhai943/sub-lang/releases)
2. Open VS Code
3. Go to Extensions (`Ctrl+Shift+X`)
4. Click the `...` menu at the top
5. Select "Install from VSIX..."
6. Choose the downloaded `.vsix` file

### From Source

```bash
cd .vscode-extension
npm install
npm install -g vsce
vsce package
code --install-extension sub-language-support-1.1.0.vsix
```

## Usage

1. Open any `.sb` or `.sub` file
2. Syntax highlighting will be applied automatically
3. Use snippets by typing the prefix and pressing `Tab`
4. Enjoy coding in SUB! 🚀

## Example

```sub
# Hello World in SUB
var greeting = "Hello, World!"

function greet(name) {
    print("Hello, " + name + "!")
}

greet("Developer")

# For loop example
for i in range(5) {
    print(i)
}

# Embedded Python code
embed python
    print("This is Python code!")
endembed
```

## About SUB Language

SUB is a multi-language compiler that allows you to write code once and compile to multiple languages:

- Python 🐍
- JavaScript 💛
- Java ☕
- C 🔧
- C++ ⚡
- Swift 🍎
- Kotlin 🎯
- Go 🔵
- Ruby 🔴

## Links

- [GitHub Repository](https://github.com/subhobhai943/sub-lang)
- [Documentation](https://github.com/subhobhai943/sub-lang/blob/main/README.md)
- [Contribution Guide](https://github.com/subhobhai943/sub-lang/blob/main/table.md)
- [Language Specification](https://github.com/subhobhai943/sub-lang/blob/main/LANGUAGE_SPEC.md)

## Contributing

Contributions are welcome! Please see our [contribution guide](https://github.com/subhobhai943/sub-lang/blob/main/table.md).

## License

MIT License - see [LICENSE](https://github.com/subhobhai943/sub-lang/blob/main/LICENSE) for details.

## Support

For issues and feature requests, please visit our [GitHub Issues](https://github.com/subhobhai943/sub-lang/issues).

---

**Made with ❤️ for the SUB Language community**
