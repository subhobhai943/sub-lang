# SUB-lang website + GitHub Pages — notes for the follow-up task

Kept brief per the assignment: the website itself is a separate task.
These are the constraints discovered while working on the repo that the
site should respect.

## Content

- The landing page should demo the three tools exactly as the README's
  Quick Start does; keep the examples in sync with `examples/` so the
  site never shows a program that does not run.
- Feature claims must match reality: native machine-code compilation is
  x86-64 **Linux** only; other platforms build through the C backend
  automatically and say so. See README "Native Compiler" and
  `docs/ROADMAP.md` for what is honest to claim.
- Objects/classes: supported through subi, subc (both backends), and the
  python/javascript/ruby transpilers; statically typed transpilers refuse
  them with a clear message.
- Networking: `net_fetch` / `net_post` / `net_download` builtins,
  `http://` only (no TLS) — document the https limitation prominently
  rather than in a footnote.

## Build/deploy

- GitHub Pages from a `gh-pages` branch or Pages workflow building a
  static site; keep the workflow in `.github/workflows/` next to ci.yml.
- A browser playground would need the interpreter compiled to WASM —
  not attempted yet; the tree has no WASM target. Treat as stretch goal.
- Use the existing logo assets: `docs/sub-lang-logo.png` and
  `docs/sub-lang-logo-mark.png`.

## Tests

- If the site includes runnable-looking snippets, CI should compile
  them: a workflow step transpiling the snippets with `./sub` catches
  drift the same way tests/conformance.py does.
