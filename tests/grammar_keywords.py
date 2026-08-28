#!/usr/bin/env python3
"""
Check that the TextMate grammar still describes the language the lexer parses.

The grammar is a second, hand-written copy of SUB's keyword list, and copies
drift. Before this check the checked-in grammar highlighted `and`, `or`, `not`
and `export` -- none of which SUB has -- while missing `switch`, `**`, the six
type keywords and the four embedded-language names. Every one of those is a
word the editor coloured wrongly, or failed to colour, for as long as the two
files disagreed.

So the lexer is the source of truth: kw_table in src/core/lexer.c, plus the
handful of words the parser recognises contextually rather than as tokens.
Adding a keyword to the lexer and not to the grammar fails here.

Usage:
    python3 tests/grammar_keywords.py
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEXER = os.path.join(ROOT, "src", "core", "lexer.c")
PARSER = os.path.join(ROOT, "src", "core", "parser_enhanced.c")
GRAMMAR = os.path.join(ROOT, ".vscode-extension", "syntaxes", "sub.tmLanguage.json")

# Recognised by the parser against an identifier's text rather than by the
# lexer against a token type, so they are not in kw_table but are still
# keywords as far as a reader -- and a highlighter -- is concerned.
CONTEXTUAL = {"switch", "match", "case", "default", "in"}


def lexer_keywords():
    """Every word in kw_table."""
    src = open(LEXER, encoding="utf-8").read()
    start = src.index("static const KWEntry kw_table[]")
    end = src.index("};", start)
    words = re.findall(r'\{\s*"([^"]+)"\s*,', src[start:end])
    if not words:
        sys.exit("could not read kw_table out of %s" % LEXER)
    return set(words)


def parser_contextual():
    """The contextual words, confirmed to still be spelled that way."""
    src = open(PARSER, encoding="utf-8").read()
    missing = [w for w in CONTEXTUAL if '"%s"' % w not in src]
    if missing:
        sys.exit("parser no longer mentions %s; update CONTEXTUAL"
                 % ", ".join(sorted(missing)))
    return set(CONTEXTUAL)


def grammar_words():
    """Every alternative in the grammar's word-boundary keyword patterns."""
    g = json.load(open(GRAMMAR, encoding="utf-8"))
    words, scoped = set(), {}

    def walk(node):
        if isinstance(node, dict):
            name, match = node.get("name", ""), node.get("match")
            if isinstance(match, str) and (
                    name.startswith("keyword.") or
                    name.startswith("storage.") or
                    name.startswith("constant.language") or
                    name.startswith("support.function")):
                for group in re.findall(r"\\b\(([^)]*)\)\\b", match):
                    for w in group.split("|"):
                        if re.fullmatch(r"[a-z_][a-z0-9_]*", w):
                            words.add(w)
                            scoped[w] = name
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)

    walk(g)
    return words, scoped


def main():
    expected = lexer_keywords() | parser_contextual()
    got, scoped = grammar_words()

    # A builtin is a function, not a keyword; `int`/`float`/`bool` are both, and
    # the grammar is right to scope those as calls where they are called.
    builtins = {w for w, s in scoped.items() if s.startswith("support.function")}

    missing = sorted(expected - got)
    # Anything the grammar colours as a keyword that the language does not have.
    invented = sorted(w for w in got - expected - builtins)

    ok = True
    if missing:
        ok = False
        print("grammar is missing %d keyword(s) the lexer has:" % len(missing))
        for w in missing:
            print("    %s" % w)
    if invented:
        ok = False
        print("grammar highlights %d word(s) SUB does not have:" % len(invented))
        for w in invented:
            print("    %s   (as %s)" % (w, scoped[w]))

    if ok:
        print("grammar covers all %d keywords and invents none" % len(expected))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
