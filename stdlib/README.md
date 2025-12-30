# SUB Standard Library

Comprehensive standard library for the SUB programming language providing common functionality across all target languages.

## 📚 Modules

### 1. I/O Module (`io.sb`)
Input/output operations and formatting.

**Functions:**
- `println(message)` - Print with newline
- `printf(format, ...args)` - Formatted print
- `readln() -> str` - Read line from stdin
- `read_int() -> int` - Read integer
- `read_float() -> float` - Read float
- `eprint(message)` - Print to stderr
- `eprintln(message)` - Print to stderr with newline
- `format(template, ...args) -> str` - Format string with placeholders
- `print_color(message, color)` - ANSI colored output
- `print_success(message)` - Green success message
- `print_error(message)` - Red error message
- `print_warning(message)` - Yellow warning
- `print_info(message)` - Cyan info message
- `clear_screen()` - Clear console
- `prompt(message) -> str` - Prompt for input
- `confirm(message) -> bool` - Yes/no confirmation

### 2. Math Module (`math.sb`)
Mathematical operations and constants.

**Constants:**
- `PI = 3.141592653589793`
- `E = 2.718281828459045`
- `TAU = 6.283185307179586`
- `PHI = 1.618033988749895`

**Functions:**
- `abs(x)` - Absolute value
- `min(a, b)` / `max(a, b)` - Min/max
- `clamp(value, min, max)` - Clamp value
- `pow(base, exp)` - Power
- `sqrt(x)` / `cbrt(x)` - Square/cube root
- `floor(x)` / `ceil(x)` / `round(x)` - Rounding
- `sin(x)` / `cos(x)` / `tan(x)` - Trigonometry
- `deg_to_rad(degrees)` / `rad_to_deg(radians)` - Conversion
- `ln(x)` / `log10(x)` / `log2(x)` - Logarithms
- `exp(x)` - Exponential
- `sum(arr)` / `mean(arr)` / `median(arr)` - Statistics
- `variance(arr)` / `std_dev(arr)` - Statistical measures
- `random()` / `randint(min, max)` / `choice(arr)` - Random
- `gcd(a, b)` / `lcm(a, b)` - GCD/LCM
- `factorial(n)` / `fibonacci(n)` - Sequences

### 3. String Module (`string.sb`)
String manipulation and processing.

**Functions:**
- `len(s)` - Length
- `concat(s1, s2)` - Concatenation
- `substr(s, start, length)` - Substring
- `strcmp(s1, s2)` / `stricmp(s1, s2)` - Comparison
- `index_of(s, ch)` / `last_index_of(s, ch)` - Search
- `contains(s, substr)` / `find(s, substr)` - Contains
- `to_upper(s)` / `to_lower(s)` - Case conversion
- `capitalize(s)` / `title_case(s)` - Capitalization
- `trim(s)` / `trim_left(s)` / `trim_right(s)` - Trimming
- `replace(s, old, new)` - Replacement
- `split(s, delimiter)` / `join(arr, separator)` - Split/join
- `reverse(s)` - Reversal
- `is_alpha(s)` / `is_digit(s)` / `is_alnum(s)` - Checks
- `starts_with(s, prefix)` / `ends_with(s, suffix)` - Prefix/suffix
- `pad_left(s, width, char)` / `pad_right(s, width, char)` - Padding
- `center(s, width, char)` - Center alignment

### 4. Collections Module (`collections.sb`)
Data structures and collection utilities.

**Array Operations:**
- `array_new(size, value)` - Create array
- `array_append(arr, value)` - Append
- `array_insert(arr, index, value)` - Insert
- `array_remove(arr, index)` - Remove
- `array_pop(arr)` - Pop last
- `array_clear(arr)` - Clear
- `array_find(arr, value)` - Find index
- `array_contains(arr, value)` - Contains
- `array_count(arr, value)` - Count occurrences

**Transformations:**
- `array_map(arr, func)` - Map
- `array_filter(arr, predicate)` - Filter
- `array_reduce(arr, func, initial)` - Reduce
- `array_sort(arr)` - Sort
- `array_reverse(arr)` - Reverse
- `array_min(arr)` / `array_max(arr)` - Min/max

**Set Operations:**
- `set_union(set1, set2)` - Union
- `set_intersection(set1, set2)` - Intersection
- `set_difference(set1, set2)` - Difference

**Stack (LIFO):**
- `stack_new()` - Create stack
- `stack_push(stack, value)` - Push
- `stack_pop(stack)` - Pop
- `stack_peek(stack)` - Peek
- `stack_is_empty(stack)` - Check empty

**Queue (FIFO):**
- `queue_new()` - Create queue
- `queue_enqueue(queue, value)` - Enqueue
- `queue_dequeue(queue)` - Dequeue
- `queue_front(queue)` - Front
- `queue_is_empty(queue)` - Check empty

**Hash Map:**
- `map_new()` - Create map
- `map_set(map, key, value)` - Set
- `map_get(map, key)` - Get
- `map_has(map, key)` - Has key
- `map_delete(map, key)` - Delete
- `map_keys(map)` / `map_values(map)` - Keys/values
- `map_size(map)` - Size

### 5. File Module (`file.sb`)
File system operations.

**File Reading:**
- `read_file(path) -> str` - Read entire file
- `read_lines(path) -> array` - Read lines
- `read_bytes(path) -> bytes` - Read binary

**File Writing:**
- `write_file(path, content)` - Write file
- `append_file(path, content)` - Append
- `write_lines(path, lines)` - Write lines

**File Checks:**
- `file_exists(path)` - Exists
- `is_file(path)` / `is_directory(path)` - Type check
- `file_size(path)` - Size

**File Operations:**
- `copy_file(src, dest)` - Copy
- `move_file(src, dest)` - Move
- `delete_file(path)` - Delete
- `rename_file(old, new)` - Rename

**Directory Operations:**
- `create_directory(path)` - Create
- `list_directory(path)` - List
- `remove_directory(path)` - Remove
- `get_current_directory()` - Get CWD
- `change_directory(path)` - Change CWD

**Path Operations:**
- `join_path(...parts)` - Join paths
- `basename(path)` - Base name
- `dirname(path)` - Directory name
- `extension(path)` - File extension
- `without_extension(path)` - Remove extension
- `normalize_path(path)` - Normalize

### 6. System Module (`system.sb`)
System utilities and environment.

**Process:**
- `exit(code)` - Exit program
- `system(command)` - Execute command
- `execute(command, args)` - Execute with args

**Environment:**
- `getenv(name)` - Get variable
- `setenv(name, value)` - Set variable
- `unsetenv(name)` - Unset variable

**Time:**
- `time()` - Unix timestamp
- `clock()` - Processor time
- `sleep(seconds)` - Sleep
- `sleep_ms(milliseconds)` - Sleep milliseconds
- `format_time(timestamp, format)` - Format
- `parse_time(str, format)` - Parse

**Platform:**
- `platform()` - Platform name
- `is_windows()` / `is_linux()` / `is_macos()` - Platform checks
- `arch()` - Architecture

**Process Info:**
- `pid()` / `ppid()` - Process IDs
- `username()` - Username
- `hostname()` - Hostname

**Performance:**
- `benchmark(func)` - Time function
- `time_it(func, iterations)` - Average time
- `memory_usage()` - Memory usage

**Command Line:**
- `argc()` - Argument count
- `argv(index)` - Get argument
- `get_args()` - All arguments

**Error Handling:**
- `assert(condition, message)` - Assertion
- `panic(message)` - Panic exit
- `last_error()` - Last error
- `errno()` - Error number

## 🚀 Usage

### Importing Modules

```sub
#import "stdlib/io.sb"
#import "stdlib/math.sb"
#import "stdlib/string.sb"

# Use functions
println("Hello, World!")
#var result = sqrt(16)
#var upper = to_upper("hello")
```

### Example: File Processing

```sub
#import "stdlib/file.sb"
#import "stdlib/string.sb"
#import "stdlib/io.sb"

#func process_file(path) {
    #if !file_exists(path) {
        print_error("File not found: " + path)
        #return
    }
    
    #var lines = read_lines(path)
    #var processed = array_map(lines, to_upper)
    write_lines("output.txt", processed)
    
    print_success("Processed " + str(len(lines)) + " lines")
}
```

### Example: Math Operations

```sub
#import "stdlib/math.sb"
#import "stdlib/io.sb"

#func stats_demo() {
    #var numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    
    println("Mean: " + str(mean(numbers)))
    println("Median: " + str(median(numbers)))
    println("Std Dev: " + str(std_dev(numbers)))
    println("Sum: " + str(sum(numbers)))
}
```

### Example: String Processing

```sub
#import "stdlib/string.sb"
#import "stdlib/io.sb"

#func text_analysis(text) {
    #var words = split(text, " ")
    #var word_count = len(words)
    #var char_count = len(text)
    
    println("Words: " + str(word_count))
    println("Characters: " + str(char_count))
    println("Uppercase: " + to_upper(text))
}
```

## 🔧 Implementation Notes

### Platform Independence
All stdlib functions are designed to work across target languages:
- **Python**: Direct mapping to built-in functions
- **JavaScript**: ES6+ compatibility
- **Java**: Standard library integration
- **C/C++**: Standard library wrappers
- **Native**: Direct system calls

### Performance
- Pure SUB implementations for algorithms
- System calls for I/O and OS operations
- Optimized for readability and correctness

### Error Handling
- Functions return `null` on error where appropriate
- Boolean return values for success/failure
- Panic/assert for critical failures

## 📝 Contributing

To add new stdlib functions:

1. Add function to appropriate module
2. Update this README with documentation
3. Add tests to `tests/stdlib/`
4. Ensure cross-platform compatibility

## 📄 License

MIT License - See LICENSE file

---

**Version**: 1.0.0  
**Last Updated**: December 30, 2025
