# SUB Language Syntax Highlighter Setup Guide

This guide will help you set up syntax highlighting for SUB Language in various editors.

## Table of Contents

- [VS Code](#vs-code)
- [GitHub](#github)
- [Sublime Text](#sublime-text)
- [Vim/Neovim](#vimneovim)
- [Building the Extension](#building-the-extension)

---

## VS Code

### Quick Installation

#### Method 1: Install from VSIX

1. **Build the extension** (if not already built):
   ```bash
   cd .vscode-extension
   npm install
   npm install -g vsce
   vsce package
   ```

2. **Install the extension**:
   ```bash
   code --install-extension sub-language-support-1.0.0.vsix
   ```

3. **Reload VS Code** and open any `.sb` file!

#### Method 2: Development Mode

1. Copy the `.vscode-extension` folder to your VS Code extensions directory:
   
   **Linux/macOS**:
   ```bash
   cp -r .vscode-extension ~/.vscode/extensions/sub-language-support
   ```
   
   **Windows**:
   ```powershell
   xcopy /E /I .vscode-extension %USERPROFILE%\.vscode\extensions\sub-language-support
   ```

2. Reload VS Code (`Ctrl+Shift+P` → "Developer: Reload Window")

3. Open any `.sb` file to see syntax highlighting!

### Features Available

✅ Syntax highlighting for all SUB keywords  
✅ String and number highlighting  
✅ Comment support  
✅ Code snippets (type `var`, `func`, `if`, etc.)  
✅ Auto-closing brackets and quotes  
✅ Code folding for functions and loops  
✅ Smart indentation  
✅ Embedded language support (Python, JavaScript, Ruby, etc.)  

### Testing the Syntax Highlighter

Create a test file `test.sb`:

```sub
# This is a comment
#var message = "Hello, SUB Language!"
#var count = 42

#function greet(name)
    #if name != ""
        #print("Hello, " + name + "!")
    #else
        #print("Hello, World!")
    #end
#end

#for i in range(5)
    #greet("User " + i)
#end

#embed ruby
    puts "This is Ruby!"
#end
```

Open this file in VS Code and you should see:
- Keywords in purple/blue
- Strings in orange/red
- Numbers in green
- Comments in gray
- Function names highlighted

---

## GitHub

GitHub will automatically detect `.sb` files using the `.gitattributes` file in the repository.

### Configuration

The `.gitattributes` file is already configured:

```gitattributes
*.sb linguist-language=SUB
*.sub linguist-language=SUB
```

And `.github/linguist.yml` defines SUB as a language:

```yaml
SUB:
  type: programming
  color: "#FF6B6B"
  extensions:
    - ".sb"
    - ".sub"
  tm_scope: source.sub
  ace_mode: text
```

### Viewing on GitHub

When you push `.sb` files to GitHub:
1. They will be recognized as SUB language
2. Language statistics will show SUB
3. Files will have basic syntax highlighting

---

## Sublime Text

### Installation

1. **Locate Sublime Text packages directory**:
   - Linux: `~/.config/sublime-text/Packages/User/`
   - macOS: `~/Library/Application Support/Sublime Text/Packages/User/`
   - Windows: `%APPDATA%\Sublime Text\Packages\User\`

2. **Copy syntax file**:
   ```bash
   cp .vscode-extension/syntaxes/sub.tmLanguage.json "<packages-dir>/SUB.sublime-syntax"
   ```

3. **Restart Sublime Text**

4. **Set syntax** for `.sb` files:
   - Open a `.sb` file
   - View → Syntax → SUB

---

## Vim/Neovim

### Installation

1. **Create syntax directory**:
   ```bash
   mkdir -p ~/.vim/syntax
   mkdir -p ~/.vim/ftdetect
   ```

2. **Create syntax file** `~/.vim/syntax/sub.vim`:
   ```vim
   " Vim syntax file for SUB Language
   if exists("b:current_syntax")
     finish
   endif

   " Keywords
   syn keyword subKeyword #var #function #if #elif #else #for #while #break #continue #return #print #input #end #import #class #embed
   syn keyword subBoolean true false null nil True False None
   syn keyword subType int float string bool list dict array

   " Strings
   syn region subString start='"' end='"' contains=subEscape
   syn region subString start="'" end="'" contains=subEscape
   syn match subEscape "\\\\[nrt'\"\\]" contained

   " Numbers
   syn match subNumber "\<\d\+\>"
   syn match subFloat "\<\d\+\\.\d\+\>"
   syn match subHex "\<0[xX][0-9a-fA-F]\+\>"
   syn match subBinary "\<0[bB][01]\+\>"

   " Comments
   syn match subComment "#\(\(var\|function\|if\|elif\|else\|for\|while\|end\)\)\@!.*$"

   " Operators
   syn match subOperator "+\|-\|\*\|/\|%\|==\|!=\|<\|>\|<=\|>="

   " Highlighting
   hi def link subKeyword Statement
   hi def link subBoolean Boolean
   hi def link subType Type
   hi def link subString String
   hi def link subEscape SpecialChar
   hi def link subNumber Number
   hi def link subFloat Float
   hi def link subHex Number
   hi def link subBinary Number
   hi def link subComment Comment
   hi def link subOperator Operator

   let b:current_syntax = "sub"
   ```

3. **Create filetype detection** `~/.vim/ftdetect/sub.vim`:
   ```vim
   au BufRead,BufNewFile *.sb set filetype=sub
   au BufRead,BufNewFile *.sub set filetype=sub
   ```

4. **Reload Vim** and open a `.sb` file!

### Neovim with Tree-sitter (Advanced)

For better highlighting with Neovim:

1. Install `nvim-treesitter`
2. Create a Tree-sitter parser for SUB (advanced)
3. Configure in `init.lua`

---

## Building the Extension

### Prerequisites

```bash
node --version  # v14 or higher
npm --version   # v6 or higher
```

### Build Steps

1. **Navigate to extension directory**:
   ```bash
   cd .vscode-extension
   ```

2. **Install dependencies**:
   ```bash
   npm install
   ```

3. **Install VSCE** (VS Code Extension Manager):
   ```bash
   npm install -g vsce
   ```

4. **Package the extension**:
   ```bash
   vsce package
   ```

   This creates `sub-language-support-1.0.0.vsix`

5. **Install locally**:
   ```bash
   code --install-extension sub-language-support-1.0.0.vsix
   ```

### Publishing to VS Code Marketplace (Optional)

1. **Create a publisher account** at [Visual Studio Marketplace](https://marketplace.visualstudio.com/)

2. **Generate a Personal Access Token** from Azure DevOps

3. **Login with VSCE**:
   ```bash
   vsce login <publisher-name>
   ```

4. **Publish**:
   ```bash
   vsce publish
   ```

---

## Testing Color Themes

Test how SUB syntax looks in different themes:

1. Open VS Code settings: `Ctrl+,`
2. Search for "Color Theme"
3. Try different themes:
   - Dark+ (default dark)
   - Light+ (default light)
   - Monokai
   - Solarized Dark
   - One Dark Pro

---

## Troubleshooting

### Syntax highlighting not working

1. **Check file extension**: Ensure file ends with `.sb` or `.sub`
2. **Reload VS Code**: `Ctrl+Shift+P` → "Developer: Reload Window"
3. **Check language mode**: Bottom right corner should show "SUB"
4. **Reinstall extension**: Uninstall and reinstall the extension

### Snippets not working

1. Type snippet prefix (e.g., `var`)
2. Press `Tab` (not Enter)
3. Check if "Editor: Tab Completion" is enabled in settings

### Extension not found

1. Check installation path:
   ```bash
   ls ~/.vscode/extensions/ | grep sub
   ```
2. Verify VSIX file was created successfully
3. Try manual installation via VS Code UI

---

## Contributing

Want to improve the syntax highlighter?

1. Edit `.vscode-extension/syntaxes/sub.tmLanguage.json`
2. Add new patterns or improve existing ones
3. Test with various SUB code samples
4. Submit a pull request!

### Adding New Keywords

In `sub.tmLanguage.json`:

```json
{
  "name": "keyword.control.sub",
  "match": "\\b(#newkeyword)\\b"
}
```

### Adding New Snippets

In `.vscode-extension/snippets/sub.json`:

```json
"Snippet Name": {
  "prefix": "shortcut",
  "body": [
    "#code ${1:placeholder}",
    "#end"
  ],
  "description": "Description"
}
```

---

## Resources

- [TextMate Grammar Guide](https://macromates.com/manual/en/language_grammars)
- [VS Code Extension API](https://code.visualstudio.com/api)
- [Regex101](https://regex101.com/) - Test regex patterns
- [SUB Language Documentation](https://github.com/subhobhai943/sub-lang)

---

**Happy coding with SUB Language! 🚀**
