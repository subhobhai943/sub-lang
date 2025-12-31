# SUB Language - Quick Start Guide

## 🚀 Get Started in 5 Minutes!

### Step 1: Clone and Build

```bash
git clone https://github.com/subhobhai943/sub-lang.git
cd sub-lang
make
```

This creates two compilers:
- `subc` - Native compiler (machine code)
- `sublang` - Transpiler (multi-language)

### Step 2: Write Your First Program

Create `hello.sb`:
```sub
#var name = "World"
#print("Hello, " + name + "!")
#print("SUB is awesome!")
```

### Step 3: Compile (Two Ways!)

#### Way 1: Native Compilation (Recommended)

```bash
# Compile to native binary
./subc hello.sb

# Run it!
./a.out
# Output:
# Hello, World!
# SUB is awesome!
```

**Why this is amazing:**
- ⚡ Runs at C speed
- 📦 No dependencies
- 🚀 Standalone executable

#### Way 2: Transpile to Other Languages

```bash
# Transpile to Python
./sublang hello.sb python
python output.py

# Transpile to JavaScript
./sublang hello.sb javascript
node output.js

# Transpile to Java
./sublang hello.sb java
javac SubProgram.java && java SubProgram
```

## 🔥 More Examples

### Arithmetic

```sub
#var x = 10
#var y = 20
#var sum = x + y
#var product = x * y

#print(sum)      # 30
#print(product)  # 200
```

### Conditionals

```sub
#var age = 21

#if age >= 18
    #print("Adult")
#else
    #print("Minor")
#end
```

### Functions

```sub
#function greet(name)
    #print("Hello, " + name)
#end

#greet("Alice")
#greet("Bob")
```

### Loops

```sub
#for i in range(5)
    #print(i)
#end

#var count = 0
#while count < 3
    #print(count)
    #count = count + 1
#end
```

## ⚙️ Advanced Usage

### Native Compilation with Optimization

```bash
# Maximum optimization
./subc program.sb -O3 -o myapp

# View assembly
./subc program.sb -S -o program.s
cat program.s

# View IR (intermediate representation)
./subc program.sb -emit-ir
```

### Transpilation to Multiple Languages

```bash
# All supported languages:
./sublang program.sb c
./sublang program.sb cpp
./sublang program.sb python
./sublang program.sb java
./sublang program.sb javascript
./sublang program.sb typescript
./sublang program.sb swift
./sublang program.sb kotlin
./sublang program.sb rust
./sublang program.sb ruby
./sublang program.sb go
```

## 🤔 When to Use Which?

### Use Native Compiler (`subc`) When:
- ✅ You want maximum performance
- ✅ You need standalone executables
- ✅ You're building desktop/server apps
- ✅ You want C-like speed

### Use Transpiler (`sublang`) When:
- ✅ You need platform-specific integration
- ✅ You're learning other languages
- ✅ You want ecosystem compatibility
- ✅ You're targeting web/mobile

## 📊 Performance Comparison

**Fibonacci(30) Benchmark:**

```bash
# Native compilation
./subc fib.sb -O3
time ./a.out
# ~12ms  ⚡

# Python transpilation
./sublang fib.sb python
time python output.py
# ~420ms  🐌 (35x slower)
```

**Conclusion**: Native SUB runs at near-C speed!

## 📖 Next Steps

1. Read [NATIVE_COMPILATION.md](NATIVE_COMPILATION.md) for deep dive
2. Check [examples/](examples/) for more code
3. Read [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) for full syntax
4. Join our community!

## 🐛 Troubleshooting

### Build Errors

```bash
# Make sure you have gcc installed
gcc --version

# Clean and rebuild
make clean
make
```

### Runtime Errors

```bash
# Check your syntax
./subc yourfile.sb -v

# View generated IR
./subc yourfile.sb -emit-ir
```

## ✨ That's It!

You now have:
- A native compiler that creates fast binaries
- A transpiler that works with 10+ languages
- The easiest syntax ever created

**Welcome to SUB!** 🎉
