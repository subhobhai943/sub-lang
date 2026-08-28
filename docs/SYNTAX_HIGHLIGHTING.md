# Syntax highlighting for SUB

Two different systems colour SUB code, and they work in completely different
ways. Most confusion about highlighting comes from treating them as one.

| Where | What colours it | Configured by |
|---|---|---|
| GitHub (files, diffs, code blocks) | GitHub Linguist, using a grammar **it** ships | `.gitattributes` |
| VS Code | The SUB extension, using the grammar **in this repo** | `.vscode-extension/` |

---

## GitHub

### What GitHub will and will not do

GitHub highlights code with [Linguist][linguist], which knows a fixed set of
languages listed in its own `languages.yml`, each paired with a grammar
Linguist ships. **There is no way to give GitHub a grammar from inside your
repository.** A `.tmLanguage.json` file committed here, however correct, is
read by nothing on github.com.

This is the part that is easy to get wrong, because getting it wrong is
silent. `.gitattributes` used to say:

```gitattributes
*.sb linguist-language=SUB linguist-detectable=true
```

`SUB` is not a language Linguist has, so the override was ignored and `.sb`
files rendered as plain grey text. Nothing errored; the setting simply did
nothing. A repository can carry a broken highlighting setup for a long time
without any signal that it is broken.

### What this repository does instead

Point `.sb` at a language Linguist *does* have, and pick the one whose grammar
fits SUB best:

```gitattributes
*.sb linguist-language=CoffeeScript linguist-detectable=false
```

CoffeeScript was measured, not guessed. Running thirteen candidate grammars
over every `.sb` file in the repository and scoring them against the token
stream SUB's own lexer produces:

| Grammar | comments | strings | numbers | keywords | literals | overall |
|---|---|---|---|---|---|---|
| **CoffeeScript** | **100%** | **100%** | 90% | **72%** | 92% | **86%** |
| Julia | 100% | 100% | 100% | 57% | 63% | 82% |
| Swift | 0% | 100% | 100% | 77% | 63% | 80% |
| TypeScript | 0% | 100% | 100% | 72% | 100% | 79% |
| Rust | 0% | 100% | 100% | 69% | 63% | 77% |
| Python | 100% | 100% | 100% | 39% | 0% | 73% |
| Zig | 0% | 100% | 100% | 48% | 100% | 70% |
| Go | 0% | 100% | 89% | 48% | 50% | 65% |

The split is comments. SUB comments start with `#`, which rules out every
C-family grammar however well its keywords line up — Rust has `fn` and `let`
but would leave every comment in the file unhighlighted. CoffeeScript is the
one grammar that gets `#` comments, strings, numbers *and* most of SUB's
keywords (`let`, `if`, `else`, `for`, `in`, `while`, `return`, `break`,
`continue`, `switch`, `case`, `do`, `true`, `false`, `null`), and it never
mistakes SUB code for a string or a regex the way the Go grammar does.

`fn` is not a CoffeeScript keyword, but `fn name(...)` parses as a call there,
so it still comes out coloured.

### Why `linguist-detectable=false`

The alias borrows CoffeeScript's grammar. It should not borrow its *name*.
With detection left on, the repository's language bar would report a compiler
written in C as a CoffeeScript project. Turning it off keeps `.sb` out of the
statistics while keeping the highlighting.

### Code blocks in Markdown

Fenced blocks are matched against Linguist's language names and aliases too,
so ```` ```sub ```` renders grey for the same reason. Use an alias of whatever
`*.sb` maps to:

````markdown
```coffee
let name = "SUB"

fn greet(who) {
    return "Hello, " + who + "!"
}

println(greet(name))
```
````

### Getting SUB itself into the language bar

The real fix is upstream: a language in Linguist gets its own name, its own
colour, and its own grammar. Linguist's bar for accepting one is adoption —
the language needs to be [in use across a substantial number of public
repositories][linguist-new], with a grammar and a file extension that does not
collide. `.sb` is currently unclaimed by any Linguist language, which is the
easy half. Until the adoption half is met, the alias above is what works.

---

## VS Code

`.vscode-extension/` is a real extension and its grammar really is used.
`syntaxes/sub.tmLanguage.json` is the only grammar in this repository that
anything loads.

Its keyword lists are a hand-written second copy of the language, and copies
drift. This one had: `and`, `or`, `not` and `export` — none of which SUB has —
while missing `switch`, `**`, all six type keywords and the four
embedded-language names. `tests/grammar_keywords.py` now compares the grammar
against `kw_table` in `src/core/lexer.c` and fails on a mismatch in either
direction, so adding a keyword to the lexer and forgetting the grammar breaks
the build.

```console
$ python3 tests/grammar_keywords.py
grammar covers all 56 keywords and invents none
```

Note that `switch`, `match`, `case`, `default` and `in` are recognised by the
parser against an identifier's text rather than by the lexer as token types,
so they are not in `kw_table`; the check keeps its own list of those and
verifies the parser still spells them that way.

---

## Language reference

What the grammar colours, and what SUB actually has:

**Comments** — `#` to end of line, `//` to end of line, and `/* */` blocks
(which nest). `#embed` and `#endembed` open an embedded block and are not
comments.

**Declarations** — `let`, `const`, `var`, `fn`, `function`, `def`, `class`,
`extends`, `implements`, `import`. (`var`, `function` and `def` are
deprecated spellings of `let` and `fn`; the compiler says so once per run.)

**Control flow** — `if`, `elif`, `else`, `for`, `while`, `do`, `end`,
`break`, `continue`, `return`, `switch` (or `match`), `case`, `default`,
`in`, `try`, `catch`, `finally`, `throw`.

**Types** — `int`, `float`, `string`, `bool`, `auto`, `void`.

**Literals** — `true`, `false`, `null` (`nil` is the deprecated spelling).

**Operators** — `+ - * / % **`, `== != < > <= >=`, `&& || !`,
`= += -= *= /=`.

SUB has no `and`, `or` or `not` keywords; the logical operators are `&&`,
`||` and `!`.

---

## Checking it

`.github/workflows/syntax-highlighting.yml` verifies all of the above on every
push, and each step asserts something that would have caught the original
breakage:

- the language named in `.gitattributes` resolves in a real Linguist install,
  and has a grammar
- `github-linguist` classifies a real `.sb` file as something other than
  plain text
- no Markdown fence uses a language Linguist does not know
- the grammar is valid JSON, covers every lexer keyword, and invents none
- the VS Code manifest loads that grammar and claims `.sb`

[linguist]: https://github.com/github-linguist/linguist
[linguist-new]: https://github.com/github-linguist/linguist/blob/main/CONTRIBUTING.md#adding-a-language
