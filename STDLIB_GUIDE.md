# SUB Standard Library Guide

Comprehensive guide to using the SUB standard library in your programs.

## 📋 Table of Contents

- [Overview](#overview)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Module Reference](#module-reference)
- [Examples](#examples)
- [Best Practices](#best-practices)
- [Contributing](#contributing)

## Overview

The SUB standard library provides a rich set of functions for common programming tasks:

- **I/O**: Console input/output, formatting, colored text
- **Math**: Mathematical functions, statistics, random numbers
- **String**: Text manipulation, searching, formatting
- **Collections**: Arrays, stacks, queues, maps
- **File**: File system operations, path utilities
- **System**: OS interaction, environment, timing

### Design Principles

1. **Cross-Platform**: Works across all SUB target languages
2. **Pythonic**: Clean, readable API
3. **Comprehensive**: Covers common use cases
4. **Efficient**: Optimized implementations
5. **Safe**: Error handling and validation

## Installation

The standard library is included with SUB. No additional installation needed.

```bash
# Clone SUB
git clone https://github.com/subhobhai943/sub-lang.git
cd sub-lang

# Standard library is in stdlib/
ls stdlib/
# Output: io.sb math.sb string.sb collections.sb file.sb system.sb
```

## Quick Start

### Hello World with Stdlib

```sub
#import "stdlib/io.sb"

println("Hello, World!")
print_success("Program completed!")
```

### Simple Calculator

```sub
#import "stdlib/io.sb"
#import "stdlib/math.sb"

#func calculator() {
    println("Simple Calculator")
    
    #var a = read_float()
    #var b = read_float()
    
    println("Sum: " + str(a + b))
    println("Product: " + str(a * b))
    println("Power: " + str(pow(a, b)))
}
```

### File Counter

```sub
#import "stdlib/file.sb"
#import "stdlib/string.sb"
#import "stdlib/io.sb"

#func count_words(path) {
    #if !file_exists(path) {
        print_error("File not found")
        #return
    }
    
    #var content = read_file(path)
    #var words = split(trim(content), " ")
    
    println("Word count: " + str(len(words)))
}
```

## Module Reference

### I/O Module

#### Formatted Output

```sub
#import "stdlib/io.sb"

# Basic printing
println("Hello")
printf("Name: {}, Age: {}\n", "Alice", 25)

# Colored output
print_success("Operation completed!")
print_error("Failed to connect")
print_warning("Memory usage high")
print_info("Server started on port 8080")

# Custom colors
print_color("Custom message", "magenta")
```

#### User Input

```sub
#import "stdlib/io.sb"

#func get_user_data() {
    #var name = prompt("Enter your name")
    #var age = read_int()
    #var confirmed = confirm("Are you sure?")
    
    #if confirmed {
        println("Welcome, " + name)
    }
}
```

### Math Module

#### Basic Math

```sub
#import "stdlib/math.sb"

#var x = -5
#var y = abs(x)          # 5
#var z = max(10, 20)     # 20
#var clamped = clamp(15, 0, 10)  # 10
```

#### Advanced Math

```sub
#import "stdlib/math.sb"

# Trigonometry
#var angle = deg_to_rad(45)
#var sine = sin(angle)
#var cosine = cos(angle)

# Logarithms
#var natural = ln(E)      # 1.0
#var log_10 = log10(100)  # 2.0
#var log_2 = log2(8)      # 3.0

# Exponential
#var exp_val = exp(2)     # e^2
```

#### Statistics

```sub
#import "stdlib/math.sb"

#var data = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

#var avg = mean(data)         # 5.5
#var mid = median(data)       # 5.5
#var var = variance(data)     # 8.25
#var std = std_dev(data)      # 2.872
```

#### Random Numbers

```sub
#import "stdlib/math.sb"

#var random_float = random()         # 0.0 to 1.0
#var dice = randint(1, 6)            # 1 to 6
#var card = choice(["A", "K", "Q"])  # Random choice
```

### String Module

#### String Manipulation

```sub
#import "stdlib/string.sb"

#var text = "  Hello World  "

# Case conversion
#var upper = to_upper(text)      # "  HELLO WORLD  "
#var lower = to_lower(text)      # "  hello world  "
#var title = title_case(text)    # "  Hello World  "

# Trimming
#var trimmed = trim(text)        # "Hello World"
```

#### String Searching

```sub
#import "stdlib/string.sb"

#var text = "Hello, World!"

#var pos = find(text, "World")   # 7
#var has = contains(text, "Hi")  # false
#var idx = index_of(text, 'o')   # 4
```

#### String Splitting/Joining

```sub
#import "stdlib/string.sb"

#var csv = "apple,banana,cherry"
#var fruits = split(csv, ",")  # ["apple", "banana", "cherry"]

#var joined = join(fruits, " | ")  # "apple | banana | cherry"
```

### Collections Module

#### Array Operations

```sub
#import "stdlib/collections.sb"

#var arr = array_new(5, 0)  # [0, 0, 0, 0, 0]
array_append(arr, 10)
array_insert(arr, 0, -1)

#var idx = array_find(arr, 10)
#var has = array_contains(arr, 5)
```

#### Functional Programming

```sub
#import "stdlib/collections.sb"

#var numbers = [1, 2, 3, 4, 5]

# Map: double each number
#var doubled = array_map(numbers, #func(x) { #return x * 2 })

# Filter: only even numbers
#var evens = array_filter(numbers, #func(x) { #return x % 2 == 0 })

# Reduce: sum all numbers
#var sum = array_reduce(numbers, #func(acc, x) { #return acc + x }, 0)
```

#### Stack & Queue

```sub
#import "stdlib/collections.sb"

# Stack (LIFO)
#var stack = stack_new()
stack_push(stack, "A")
stack_push(stack, "B")
#var top = stack_pop(stack)  # "B"

# Queue (FIFO)
#var queue = queue_new()
queue_enqueue(queue, 1)
queue_enqueue(queue, 2)
#var first = queue_dequeue(queue)  # 1
```

### File Module

#### Reading Files

```sub
#import "stdlib/file.sb"

# Read entire file
#var content = read_file("data.txt")

# Read lines
#var lines = read_lines("data.txt")
#for line in lines {
    println(line)
}

# Read binary
#var data = read_bytes("image.png")
```

#### Writing Files

```sub
#import "stdlib/file.sb"

# Write file
write_file("output.txt", "Hello, World!")

# Append to file
append_file("log.txt", "New entry\n")

# Write lines
#var lines = ["Line 1", "Line 2", "Line 3"]
write_lines("output.txt", lines)
```

#### File Operations

```sub
#import "stdlib/file.sb"

# Check existence
#if file_exists("data.txt") {
    # File operations
    copy_file("data.txt", "backup.txt")
    #var size = file_size("data.txt")
}

# Directory operations
create_directory("output")
#var files = list_directory(".")
```

#### Path Utilities

```sub
#import "stdlib/file.sb"

#var path = "/home/user/docs/file.txt"

#var name = basename(path)          # "file.txt"
#var dir = dirname(path)            # "/home/user/docs"
#var ext = extension(path)          # ".txt"
#var no_ext = without_extension(path)  # "/home/user/docs/file"

#var full = join_path("home", "user", "file.txt")
```

### System Module

#### Process Control

```sub
#import "stdlib/system.sb"

# Execute command
#var result = system("ls -la")

# Exit program
#if error_condition {
    exit(1)
}
```

#### Environment Variables

```sub
#import "stdlib/system.sb"

#var home = getenv("HOME")
#var path = getenv("PATH")

setenv("MY_VAR", "my_value")
```

#### Timing

```sub
#import "stdlib/system.sb"

#var start = clock()
# ... do work ...
#var end = clock()
#var elapsed = end - start

sleep(1)  # Sleep for 1 second
```

#### Platform Detection

```sub
#import "stdlib/system.sb"

#if is_windows() {
    println("Running on Windows")
} #elif is_linux() {
    println("Running on Linux")
} #elif is_macos() {
    println("Running on macOS")
}

#var arch_name = arch()  # "x86_64", "arm64", etc.
```

## Examples

### Complete Program: Text Analyzer

```sub
#import "stdlib/file.sb"
#import "stdlib/string.sb"
#import "stdlib/collections.sb"
#import "stdlib/io.sb"

#func analyze_text(path) {
    # Read file
    #if !file_exists(path) {
        print_error("File not found: " + path)
        #return
    }
    
    #var content = read_file(path)
    
    # Count words
    #var words = split(trim(content), " ")
    #var word_count = len(words)
    
    # Count lines
    #var lines = read_lines(path)
    #var line_count = len(lines)
    
    # Most common words
    #var word_freq = map_new()
    #for word in words {
        #var lower = to_lower(word)
        #if map_has(word_freq, lower) {
            map_set(word_freq, lower, map_get(word_freq, lower) + 1)
        } #else {
            map_set(word_freq, lower, 1)
        }
    }
    
    # Display results
    print_info("Text Analysis Results")
    println("="  * 40)
    println("Lines: " + str(line_count))
    println("Words: " + str(word_count))
    println("Characters: " + str(len(content)))
    println("Unique words: " + str(map_size(word_freq)))
    
    print_success("Analysis complete!")
}
```

### Complete Program: Simple HTTP Server Monitor

```sub
#import "stdlib/system.sb"
#import "stdlib/io.sb"
#import "stdlib/file.sb"

#func monitor_server(url, interval) {
    println("Monitoring: " + url)
    
    #while true {
        #var start = time()
        #var result = system("curl -s -o /dev/null -w '%{http_code}' " + url)
        #var end = time()
        #var elapsed = end - start
        
        #if result == 200 {
            print_success("Server OK (" + str(elapsed) + "ms)")
        } #else {
            print_error("Server Error: " + str(result))
            
            # Log error
            #var log = format_time(time(), "%Y-%m-%d %H:%M:%S") + 
                       " - Error: " + str(result) + "\n"
            append_file("monitor.log", log)
        }
        
        sleep(interval)
    }
}
```

## Best Practices

### 1. Import Only What You Need

```sub
# Good: Import specific modules
#import "stdlib/io.sb"
#import "stdlib/math.sb"

# Avoid: Importing everything
```

### 2. Error Handling

```sub
#import "stdlib/file.sb"
#import "stdlib/io.sb"

#func safe_read(path) {
    #if !file_exists(path) {
        print_error("File not found")
        #return null
    }
    
    #var content = read_file(path)
    #if content == null {
        print_error("Failed to read file")
        #return null
    }
    
    #return content
}
```

### 3. Use Assertions

```sub
#import "stdlib/system.sb"

#func divide(a, b) {
    assert(b != 0, "Division by zero")
    #return a / b
}
```

### 4. Benchmark Performance

```sub
#import "stdlib/system.sb"

#func test_performance() {
    #var time = benchmark(#func() {
        # Your code here
    })
    
    println("Execution time: " + str(time) + "s")
}
```

## Contributing

Want to add to the standard library?

1. Create a new module in `stdlib/`
2. Follow naming conventions
3. Add comprehensive documentation
4. Write tests
5. Submit a pull request

## License

MIT License - See LICENSE file

---

**Version**: 1.0.0  
**SUB Language**: v1.0.5+  
**Last Updated**: December 30, 2025
