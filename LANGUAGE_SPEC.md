# SUB Language Specification v2.0

## 1. Introduction

SUB (Simple Universal Builder) is a cross-platform programming language designed for ease of learning and maximum portability. It uses a blockchain-inspired syntax with hash symbols (#) to create visually clear, chained code blocks.

### What's New in v2.0
- **C++ Embedding**: Native C++ code support alongside C
- **Enhanced Error Handling**: Try-catch-finally blocks with stack traces
- **Improved Type System**: Stronger type inference and checking
- **Modern Features**: Async/await foundations, better memory management

## 2. Syntax

### 2.1 Comments
```sub
// Single-line comment

/* Multi-line
   comment */
```

### 2.2 Variables
Variables are declared using the `#var` keyword with optional type hints:
```sub
#var name = "John"              // String (inferred)
#var age = 25                   // Number (inferred)
#var price:float = 19.99        // Explicit type
#var isActive:bool = true       // Boolean with type
#var data:auto = getData()      // Auto type inference
```

### 2.3 Data Types
- **String**: Text enclosed in quotes ("" or '')
- **Integer**: Whole numbers (int8, int16, int32, int64)
- **Float**: Floating-point (float32, float64)
- **Boolean**: true or false
- **Array**: Ordered collection [1, 2, 3]
- **Object**: Key-value pairs {key: value}
- **Null**: null value
- **Auto**: Automatic type inference

### 2.4 Functions
```sub
#function functionName(param1:int, param2:string):string
    // function body
    #return result
#end

// Arrow functions (inline)
#var square = (x:int) => x * x

// Generic functions
#function identity<T>(value:T):T
    #return value
#end
```

### 2.5 Conditionals
```sub
#if condition
    // code
#elif otherCondition
    // code
#else
    // code
#end

// Ternary operator
#var result = condition ? value1 : value2
```

### 2.6 Loops

**For Loop:**
```sub
#for i in range(10)
    #print(i)
#end

// For-each loop
#for item in collection
    #print(item)
#end

// C-style for loop
#for (i = 0; i < 10; i++)
    #print(i)
#end
```

**While Loop:**
```sub
#while condition
    // code
#end

// Do-while loop
#do
    // code
#while condition
```

### 2.7 Error Handling
```sub
#try
    // risky code
    #var result = divide(10, 0)
#catch DivisionError as e
    #print("Error: " + e.message)
    #print(e.stackTrace)
#catch TypeError as e
    #print("Type error: " + e.message)
#finally
    // cleanup code (always executes)
    #print("Cleanup complete")
#end

// Throw custom errors
#throw CustomError("Something went wrong")
```

### 2.8 UI Components
```sub
#ui.window(title="App", width=800, height=600)
    #ui.button(text="Click", onclick=handler)
    #ui.label(text="Hello")
    #ui.input(placeholder="Name", id="input1")
    #ui.slider(min=0, max=100, value=50)
#end
```

### 2.9 Embedded Languages

**C++ Embedding:**
```sub
#embed cpp
    #include <vector>
    #include <algorithm>
    
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    std::sort(numbers.begin(), numbers.end());
    
    // Export function to SUB
    extern "C" int getSum() {
        return std::accumulate(numbers.begin(), numbers.end(), 0);
    }
#endembed

// Call C++ function from SUB
#var total = getSum()
```

**C Embedding:**
```sub
#embed c
    #include <stdio.h>
    #include <math.h>
    
    double calculate(double x) {
        return sqrt(x) + pow(x, 2);
    }
#endembed
```

**Python Embedding:**
```sub
#embed python
    import numpy as np
    import pandas as pd
    
    data = np.array([1, 2, 3, 4, 5])
    mean_value = np.mean(data)
#endembed
```

**JavaScript Embedding:**
```sub
#embed javascript
    const items = [1, 2, 3, 4, 5];
    const doubled = items.map(x => x * 2);
    console.log(doubled);
#endembed
```

**Rust Embedding (New):**
```sub
#embed rust
    fn fibonacci(n: u32) -> u32 {
        match n {
            0 => 0,
            1 => 1,
            _ => fibonacci(n-1) + fibonacci(n-2)
        }
    }
#endembed
```

## 3. Blockchain Method

The hash symbol (#) serves as a "blockchain" operator that:
- Marks the start of each statement
- Creates visual chaining of operations
- Improves code readability
- Enforces structured programming

## 4. Cross-Platform Compilation

SUB compiles to native code for multiple platforms:

| Platform | Output | Framework | Performance |
|----------|--------|-----------|-------------|
| Android | Java/Kotlin | Android SDK | Native |
| iOS | Swift | UIKit | Native |
| Web | JavaScript/WASM | HTML5 | Fast |
| Windows | C/C++ | Win32 | Native |
| macOS | Swift/ObjC | Cocoa | Native |
| Linux | C/C++ | GTK | Native |

## 5. Standard Library

### 5.1 I/O Operations
- `print(value)` - Output to console
- `input(prompt)` - Get user input
- `readFile(path)` - Read file contents
- `writeFile(path, content)` - Write to file
- `appendFile(path, content)` - Append to file

### 5.2 String Operations
- `length(str)` - String length
- `concat(str1, str2)` - Concatenate
- `substring(str, start, end)` - Extract substring
- `split(str, delimiter)` - Split string
- `replace(str, old, new)` - Replace text
- `trim(str)` - Remove whitespace
- `toUpper(str)` - Convert to uppercase
- `toLower(str)` - Convert to lowercase

### 5.3 Math Operations
- `abs(x)` - Absolute value
- `sqrt(x)` - Square root
- `pow(x, y)` - Power
- `floor(x)` - Floor function
- `ceil(x)` - Ceiling function
- `round(x)` - Round to nearest integer
- `random()` - Random number (0-1)
- `randomInt(min, max)` - Random integer
- `sin(x)`, `cos(x)`, `tan(x)` - Trigonometric functions

### 5.4 Array/Collection Operations
- `push(arr, item)` - Add to end
- `pop(arr)` - Remove from end
- `shift(arr)` - Remove from start
- `unshift(arr, item)` - Add to start
- `slice(arr, start, end)` - Extract subset
- `map(arr, func)` - Transform elements
- `filter(arr, func)` - Filter elements
- `reduce(arr, func, initial)` - Reduce to single value
- `sort(arr)` - Sort array
- `reverse(arr)` - Reverse array

### 5.5 UI Operations
- `ui.alert(message)` - Show alert
- `ui.confirm(message)` - Show confirmation
- `ui.prompt(message)` - Get input
- `ui.getInput(id)` - Get input value
- `ui.setProperty(id, prop, value)` - Set property
- `ui.addClass(id, className)` - Add CSS class
- `ui.removeClass(id, className)` - Remove CSS class

### 5.6 Network Operations (New)
- `http.get(url)` - HTTP GET request
- `http.post(url, data)` - HTTP POST request
- `http.put(url, data)` - HTTP PUT request
- `http.delete(url)` - HTTP DELETE request
- `ws.connect(url)` - WebSocket connection

### 5.7 Date/Time Operations (New)
- `now()` - Current timestamp
- `date.format(timestamp, format)` - Format date
- `date.parse(string, format)` - Parse date
- `date.add(date, amount, unit)` - Add time
- `date.diff(date1, date2, unit)` - Date difference

## 6. Memory Management

### 6.1 Automatic Memory Management
SUB uses reference counting with cycle detection for automatic memory management:
```sub
#var data = allocate(1000)  // Automatically managed
// Memory freed when data goes out of scope
```

### 6.2 Manual Memory Control (Advanced)
```sub
#var ptr = malloc(size)     // Manual allocation
#free(ptr)                  // Manual deallocation
```

## 7. Performance Features

### 7.1 Inline Assembly (Advanced)
```sub
#asm x86_64
    mov rax, 1
    xor rbx, rbx
#endasm
```

### 7.2 SIMD Operations
```sub
#var vec1 = simd.vector([1, 2, 3, 4])
#var vec2 = simd.vector([5, 6, 7, 8])
#var result = simd.add(vec1, vec2)  // Parallel addition
```

### 7.3 Parallel Processing
```sub
#parallel for i in range(1000)
    // Runs in parallel threads
    #process(i)
#end
```

## 8. File Extension

SUB source files use the `.sb` extension.

## 9. Compiler Phases

1. **Lexical Analysis** - Tokenization
2. **Syntax Analysis** - Parsing into AST
3. **Semantic Analysis** - Type checking and validation
4. **Optimization** - Multi-pass optimization
   - Dead code elimination
   - Constant folding
   - Loop unrolling
   - Inline expansion
5. **Code Generation** - Platform-specific output
6. **Linking** - Combine with embedded code

## 10. Reserved Keywords

var, function, if, elif, else, for, while, do, return, end, embed, endembed, ui, import, from, try, catch, finally, throw, class, extends, implements, new, this, super, static, private, public, protected, async, await, yield, true, false, null, auto, const, let, in, of, break, continue, switch, case, default, interface, enum, namespace, using, template, typename, typedef, sizeof, typeof, instanceof

## 11. Build System Integration

### 11.1 Build Configuration
```sub
// build.sb - Build configuration file
#config
    name: "MyApp"
    version: "1.0.0"
    targets: ["android", "ios", "web"]
    optimize: true
    minify: true
#end
```

### 11.2 Dependencies
```sub
#dependencies
    "math-lib": "^2.0.0"
    "ui-toolkit": "1.5.3"
#end
```

## 12. Interoperability

### 12.1 C/C++ Interop
- Direct function calls to C/C++ libraries
- Zero-copy data sharing
- ABI compatibility

### 12.2 Foreign Function Interface (FFI)
```sub
#ffi
    library: "mylib.so"
    function calculateSum(a:int, b:int):int
#end

#var result = calculateSum(5, 10)
```

## 13. Debugging Features

- Source-level debugging
- Breakpoints support
- Stack trace generation
- Memory profiling
- Performance profiling

---
**Version**: 2.0  
**Last Updated**: November 2025  
**Status**: Active Development
