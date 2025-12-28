# 🎨 SUB Language Syntax Highlighting

Complete guide to syntax highlighting support for SUB Programming Language across multiple editors and platforms.

## Overview

SUB Language now has full syntax highlighting support for:

- ✅ **VS Code** - Full extension with snippets and auto-completion
- ✅ **GitHub** - Native syntax highlighting for `.sb` files
- ✅ **Sublime Text** - TextMate grammar support
- ✅ **Vim/Neovim** - Custom syntax files

---

## 📝 Features

### Syntax Elements Highlighted

| Element | Examples | Color (Default Theme) |
|---------|----------|----------------------|
| **Keywords** | `#var`, `#function`, `#if`, `#for`, `#while`, `#end` | Purple/Blue |
| **Strings** | `"hello"`, `'world'` | Orange/Red |
| **Numbers** | `42`, `3.14`, `0xFF`, `0b1010` | Green |
| **Comments** | `# This is a comment` | Gray |
| **Operators** | `+`, `-`, `*`, `/`, `==`, `!=` | White/Light |
| **Functions** | Function names and calls | Yellow |
| **Variables** | Variable declarations and usage | Light Blue |
| **Booleans** | `true`, `false`, `null` | Blue |
| **Types** | `int`, `float`, `string`, `bool` | Cyan |

### Supported Keywords

```
#var          - Variable declaration
#function     - Function declaration
#if           - If statement
#elif         - Else-if statement
#else         - Else statement
#for          - For loop
#while        - While loop
#break        - Break from loop
#continue     - Continue loop
#return       - Return from function
#print        - Print output
#input        - Get input
#end          - End block
#import       - Import module
#class        - Class declaration
#embed        - Embed code from other languages
```

---

## 🚀 Quick Start - VS Code

### Installation

#### Option 1: From VSIX (Recommended)

```bash
# Navigate to extension directory
cd .vscode-extension

# Install dependencies
npm install

# Install VSCE (if not already installed)
npm install -g vsce

# Package the extension
vsce package

# Install the extension
code --install-extension sub-language-support-1.0.0.vsix
```

#### Option 2: Development Mode

```bash
# Linux/macOS
cp -r .vscode-extension ~/.vscode/extensions/sub-language-support

# Windows (PowerShell)
xcopy /E /I .vscode-extension %USERPROFILE%\.vscode\extensions\sub-language-support

# Reload VS Code
# Press Ctrl+Shift+P -> "Developer: Reload Window"
```

### Usage

1. Open any `.sb` or `.sub` file
2. Syntax highlighting activates automatically
3. Use code snippets by typing:
   - `var` + Tab → Variable declaration
   - `func` + Tab → Function
   - `if` + Tab → If statement
   - `for` + Tab → For loop
   - And more!

---

## 💻 Code Snippets (VS Code)

### Available Snippets

| Prefix | Expands To | Description |
|--------|-----------|-------------|
| `var` | `#var name = value` | Variable declaration |
| `func` | `#function name(params) ... #end` | Function |
| `if` | `#if condition ... #end` | If statement |
| `ifelse` | `#if ... #else ... #end` | If-else |
| `ifelif` | `#if ... #elif ... #else ... #end` | If-elif-else |
| `for` | `#for i in range(n) ... #end` | For loop |
| `while` | `#while condition ... #end` | While loop |
| `print` | `#print(value)` | Print |
| `input` | `#var name = #input("prompt")` | Input |
| `class` | `#class ClassName ... #end` | Class |
| `import` | `#import module` | Import |
| `embedpy` | `#embed python ... #end` | Embed Python |
| `embedjs` | `#embed javascript ... #end` | Embed JavaScript |
| `embedrb` | `#embed ruby ... #end` | Embed Ruby |
| `return` | `#return value` | Return |
| `break` | `#break` | Break |
| `continue` | `#continue` | Continue |

### Example Usage

1. Type `func` and press **Tab**
2. Enter function name
3. Press **Tab** to move to parameters
4. Press **Tab** to move to function body
5. Write your code!

---

## 📚 Editor Features (VS Code)

### Auto-Completion

- **Auto-closing pairs**: `()`, `[]`, `{}`, `""`, `''`
- **Bracket matching**: Highlights matching brackets
- **Smart indentation**: Auto-indent after keywords

### Code Folding

Fold/unfold code blocks:

```sub
#function calculateSum(n)  # ▼ Click to fold
    #var sum = 0
    #for i in range(n)
        sum = sum + i
    #end
    #return sum
#end
```

### Comment Toggle

- Press `Ctrl+/` (Windows/Linux) or `Cmd+/` (Mac)
- Toggles `#` comment at the start of the line

---

## 🐙 GitHub Syntax Highlighting

### Configuration

GitHub automatically detects SUB files using:

**`.gitattributes`**:
```gitattributes
*.sb linguist-language=SUB linguist-detectable
*.sub linguist-language=SUB linguist-detectable
```

**`.github/linguist.yml`**:
```yaml
SUB:
  type: programming
  color: "#FF6B6B"
  extensions:
    - ".sb"
    - ".sub"
  tm_scope: source.sub
```

### Result

- `.sb` files show as "SUB" in GitHub
- Language statistics include SUB
- Syntax highlighting in code view
- Red color (#FF6B6B) in language bar

---

## 🎈 Sublime Text

### Installation

1. **Find Packages directory**:
   - Linux: `~/.config/sublime-text/Packages/User/`
   - macOS: `~/Library/Application Support/Sublime Text/Packages/User/`
   - Windows: `%APPDATA%\Sublime Text\Packages\User\`

2. **Copy syntax file**:
   ```bash
   cp .vscode-extension/syntaxes/sub.tmLanguage.json <packages-dir>/SUB.sublime-syntax
   ```

3. **Restart Sublime Text**

4. **Set syntax**: View → Syntax → SUB

---

## 💉 Vim/Neovim

### Quick Setup

```bash
# Create directories
mkdir -p ~/.vim/syntax ~/.vim/ftdetect

# Download and install syntax file
curl -o ~/.vim/syntax/sub.vim https://raw.githubusercontent.com/subhobhai943/sub-lang/main/.vscode-extension/vim/sub.vim

# Create filetype detection
echo 'au BufRead,BufNewFile *.sb,*.sub set filetype=sub' > ~/.vim/ftdetect/sub.vim

# Reload Vim
```

### Manual Setup

Create `~/.vim/syntax/sub.vim`:

```vim
" SUB Language syntax
if exists("b:current_syntax")
  finish
endif

syn keyword subKeyword #var #function #if #elif #else #for #while #end
syn keyword subBoolean true false null
syn region subString start='"' end='"'
syn match subNumber "\<\d\+\>"
syn match subComment "#.*$"

hi def link subKeyword Statement
hi def link subBoolean Boolean
hi def link subString String
hi def link subNumber Number
hi def link subComment Comment

let b:current_syntax = "sub"
```

---

## 🧪 Testing Syntax Highlighting

### Test File (`test.sb`)

Create this file to test highlighting:

```sub
# SUB Language Syntax Highlighting Test
# This file demonstrates all syntax elements

# Variable declarations
#var name = "SUB Language"
#var version = 1.0
#var count = 42
#var enabled = true

# Function declaration
#function greet(person, times)
    #for i in range(times)
        #print("Hello, " + person + "!")
    #end
#end

# Conditional statements
#function checkNumber(n)
    #if n > 0
        #print("Positive")
    #elif n < 0
        #print("Negative")
    #else
        #print("Zero")
    #end
#end

# Loop examples
#var sum = 0
#for i in range(10)
    sum = sum + i
#end

#var counter = 0
#while counter < 5
    #print(counter)
    counter = counter + 1
#end

# Operators and expressions
#var result = (10 + 5) * 2 - 3 / 1
#var isEqual = result == 27
#var isGreater = result > 20

# Embedded code
#embed python
    print("This is Python!")
    for i in range(3):
        print(f"Python loop: {i}")
#end

#embed ruby
    puts "This is Ruby!"
    (0...3).each do |i|
        puts "Ruby loop: #{i}"
    end
#end

#embed javascript
    console.log("This is JavaScript!");
    for (let i = 0; i < 3; i++) {
        console.log(`JS loop: ${i}`);
    }
#end

# Call functions
#greet("Developer", 3)
#checkNumber(42)

#print("Test completed!")
```

### Expected Highlighting

When you open `test.sb`:

- 🟣 **Purple/Blue**: `#var`, `#function`, `#if`, `#for`, `#while`, `#end`
- 🟠 **Orange/Red**: `"SUB Language"`, `"Hello"`, `"Developer"`
- 🟢 **Green**: `1.0`, `42`, `10`, `5`, `2`, `3`
- ⚪ **Gray**: `# SUB Language...`, `# Variable declarations`
- 🔵 **Blue**: `true`, `false`
- 🟡 **Yellow**: `greet`, `checkNumber`, `print`

---

## 🎯 Color Customization

### VS Code Custom Theme

Add to `settings.json`:

```json
{
  "editor.tokenColorCustomizations": {
    "textMateRules": [
      {
        "scope": "keyword.control.sub",
        "settings": {
          "foreground": "#FF79C6",
          "fontStyle": "bold"
        }
      },
      {
        "scope": "string.quoted.double.sub",
        "settings": {
          "foreground": "#F1FA8C"
        }
      },
      {
        "scope": "entity.name.function.sub",
        "settings": {
          "foreground": "#50FA7B",
          "fontStyle": "bold"
        }
      }
    ]
  }
}
```

---

## 🐛 Troubleshooting

### VS Code Issues

| Problem | Solution |
|---------|----------|
| No syntax highlighting | Check file extension is `.sb` or `.sub` |
| Extension not found | Reinstall from VSIX or copy to extensions folder |
| Snippets not working | Press Tab (not Enter) after typing prefix |
| Wrong colors | Check your VS Code theme supports TextMate scopes |

### GitHub Issues

| Problem | Solution |
|---------|----------|
| Not showing as SUB | Commit `.gitattributes` file |
| No color in stats | GitHub may take time to update statistics |
| Generic highlighting | Ensure `.github/linguist.yml` is present |

### Vim Issues

| Problem | Solution |
|---------|----------|
| No highlighting | Check `~/.vim/syntax/sub.vim` exists |
| Wrong filetype | Verify `~/.vim/ftdetect/sub.vim` is set up |
| Colors not showing | Your terminal may not support 256 colors |

---

## 📦 Extension Files Structure

```
.vscode-extension/
├── package.json              # Extension manifest
├── language-configuration.json # Editor behavior
├── extension.js              # Extension logic
├── README.md                 # Extension documentation
├── SETUP.md                  # Setup guide
├── syntaxes/
│   └── sub.tmLanguage.json   # TextMate grammar
└── snippets/
    └── sub.json              # Code snippets
```

---

## 🔧 Development

### Modifying Syntax Rules

Edit `.vscode-extension/syntaxes/sub.tmLanguage.json`:

```json
{
  "patterns": [
    {
      "name": "keyword.control.sub",
      "match": "\\b(#newkeyword)\\b"
    }
  ]
}
```

### Testing Changes

1. Make changes to `sub.tmLanguage.json`
2. Press `Ctrl+Shift+P` → "Developer: Reload Window"
3. Test with `.sb` files
4. Iterate until perfect!

### Adding New Snippets

Edit `.vscode-extension/snippets/sub.json`:

```json
"New Snippet": {
  "prefix": "shortcut",
  "body": [
    "#keyword ${1:placeholder}",
    "    ${2:body}",
    "#end"
  ],
  "description": "Description of snippet"
}
```

---

## 📚 Resources

- **SUB Language**: [GitHub Repository](https://github.com/subhobhai943/sub-lang)
- **VS Code API**: [Extension API Docs](https://code.visualstudio.com/api)
- **TextMate**: [Grammar Guide](https://macromates.com/manual/en/language_grammars)
- **Linguist**: [GitHub Linguist](https://github.com/github/linguist)
- **Regex Testing**: [Regex101](https://regex101.com/)

---

## ❤️ Contributing

Want to improve syntax highlighting?

1. Fork the repository
2. Edit syntax files
3. Test thoroughly
4. Submit a pull request!

See [CONTRIBUTING.md](table.md) for details.

---

## 📢 Feedback

Found an issue or have suggestions?

- Open an issue: [GitHub Issues](https://github.com/subhobhai943/sub-lang/issues)
- Start a discussion: [GitHub Discussions](https://github.com/subhobhai943/sub-lang/discussions)

---

**Made with 💜 for the SUB Language community!**

*Last updated: December 29, 2025*
