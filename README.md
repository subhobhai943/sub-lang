# SUB Programming Language

## World's Easiest Cross-Platform Programming Language

SUB is a revolutionary programming language designed to be the easiest language to learn and use, powered by blockchain-inspired syntax and a high-performance C compiler.

### Key Features

🔗 **Blockchain Method with #**
- Uses hash symbols (#) to create readable, chained code blocks
- Visual clarity through connected operations
- Easy to understand syntax flow

⚡ **Fast Performance**
- Compiled using C backend
- Native code generation
- Optimized execution

🌍 **True Cross-Platform**
- Android
- iOS  
- Windows
- macOS
- Linux
- Web (HTML5/JS)

🔄 **Multi-Language Support**
- Embed Python code
- Embed JavaScript code
- Embed other languages within .sb files

### File Extension
`.sb` - SUB Language files

### Quick Start

#### Installation
```bash
# Clone the repository
git clone https://github.com/subhobhai943/sub-lang.git
cd sub-lang

# Build the compiler
make
```

#### Compile a SUB program
```bash
./sub hello.sb web        # Compile for web
./sub hello.sb android    # Compile for Android
./sub hello.sb ios        # Compile for iOS
```

### Syntax Examples

#### Variables (Blockchain-style)
```sub
#var name = "John"
#var age = 25
#var balance = 1000.50
```

#### Functions
```sub
#function greet(name)
    #print("Hello, " + name)
    #return "Done"
#end
```

#### Conditionals
```sub
#if age > 18
    #print("Adult")
#else
    #print("Minor")
#end
```

#### Loops
```sub
#for i in range(10)
    #print(i)
#end
```

#### Cross-Platform UI
```sub
#ui.window(title="My App", width=800, height=600)
    #ui.button(text="Click Me")
    #ui.label(text="Welcome!")
#end
```

#### Embedded Languages
```sub
#embed python
    import numpy as np
    data = np.array([1,2,3])
#endembed

#embed javascript
    const items = [1, 2, 3];
    console.log(items);
#endembed
```

### Compiler Architecture

1. **Lexical Analysis** - Tokenize source code
2. **Syntax Analysis** - Parse tokens into AST
3. **Semantic Analysis** - Type checking and validation
4. **Optimization** - Code optimization passes
5. **Code Generation** - Platform-specific output

### Platform Targets

| Platform | Output Language | Framework |
|----------|----------------|-----------|  
| Android | Java/Kotlin | Native Android SDK |
| iOS | Swift | UIKit |
| Web | JavaScript | HTML5 |
| Windows | C/C++ | Win32 API |
| macOS | Objective-C/Swift | Cocoa |
| Linux | C | GTK |

### Why SUB is Easy to Learn

1. **Visual Clarity** - The # symbol creates a clear visual chain
2. **Consistent Syntax** - Same patterns for all constructs
3. **Familiar Concepts** - Borrows from popular languages
4. **No Boilerplate** - Minimal setup code required
5. **Cross-Platform** - Write once, deploy everywhere

### Contributing

We welcome contributions! See CONTRIBUTING.md for details.

### License

MIT License - See LICENSE file

### Contact

- GitHub: https://github.com/subhobhai943/sub-lang
- Issues: https://github.com/subhobhai943/sub-lang/issues

---
Built with ❤️ for developers who want simplicity and power
