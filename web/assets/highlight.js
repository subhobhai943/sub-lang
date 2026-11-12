/* SUB language (.sb) syntax highlighter.
 * Keywords mirror the lexer table in src/core/lexer.c; switch/case/default/match
 * are contextual identifiers handled by the parser and are matched here too. */
(function () {
  "use strict";

  var KEYWORDS = [
    "var", "const", "let", "function", "fn", "def", "return", "if", "elif",
    "else", "for", "while", "do", "end", "break", "continue", "try", "catch",
    "finally", "throw", "embed", "endembed", "cpp", "python", "javascript",
    "rust", "import", "ui", "class", "extends", "implements", "new", "this",
    "super", "static", "private", "public", "protected", "async", "await",
    "yield", "int", "float", "string", "bool", "auto", "void", "true",
    "false", "null", "nil"
  ];
  var CONTEXTUAL = ["switch", "case", "default", "match"];
  var BUILTINS = [
    "print", "show", "println", "input", "str", "to_string", "int", "float",
    "type", "len", "range", "abs", "sqrt", "min", "max", "floor", "ceil",
    "round", "push", "pop", "upper", "lower", "trim", "substring", "char_at",
    "contains", "replace", "split", "join"
  ];
  var kwRe = new RegExp("^(?:" + KEYWORDS.join("|") + ")$");
  var ctxRe = new RegExp("^(?:" + CONTEXTUAL.join("|") + ")$");
  var bRe = new RegExp("^(?:" + BUILTINS.join("|") + ")$");

  function esc(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  function isWordChar(c) {
    return /[A-Za-z0-9_]/.test(c);
  }

  function highlightSB(src) {
    var out = [];
    var i = 0, n = src.length;
    while (i < n) {
      var c = src[i];
      // comments: # and //  (also #embed / #endembed directives)
      if (c === "#" || (c === "/" && src[i + 1] === "/")) {
        var comStart = i;
        while (i < n && src[i] !== "\n") i++;
        out.push('<span class="tok-com">' + esc(src.slice(comStart, i)) + "</span>");
        continue;
      }
      // strings
      if (c === '"') {
        var sStart = i; i++;
        while (i < n && src[i] !== '"' && src[i] !== "\n") {
          if (src[i] === "\\") i++;
          i++;
        }
        if (i < n && src[i] === '"') i++;
        out.push('<span class="tok-str">' + esc(src.slice(sStart, i)) + "</span>");
        continue;
      }
      // numbers
      if (/[0-9]/.test(c) || (c === "." && /[0-9]/.test(src[i + 1] || ""))) {
        var nStart = i;
        while (i < n && /[0-9.]/.test(src[i])) i++;
        out.push('<span class="tok-num">' + esc(src.slice(nStart, i)) + "</span>");
        continue;
      }
      // identifiers / keywords
      if (/[A-Za-z_]/.test(c)) {
        var wStart = i;
        while (i < n && isWordChar(src[i])) i++;
        var w = src.slice(wStart, i);
        if (kwRe.test(w)) {
          out.push('<span class="tok-kw">' + w + "</span>");
        } else if (ctxRe.test(w)) {
          out.push('<span class="tok-kw">' + w + "</span>");
        } else if (bRe.test(w) || (src[i] === "(" && w !== "")) {
          if (bRe.test(w)) out.push('<span class="tok-fn">' + w + "</span>");
          else if (src[i] === "(" && /^[A-Za-z_][A-Za-z0-9_]*$/.test(w)) {
            out.push('<span class="tok-fn">' + w + "</span>");
          } else out.push(esc(w));
        } else {
          out.push(esc(w));
        }
        continue;
      }
      // everything else
      out.push(esc(c));
      i++;
    }
    return out.join("");
  }

  function highlightSH(src) {
    // shell: lines starting with $ are commands, rest is output
    return src.split("\n").map(function (line) {
      if (line.indexOf("$") === 0) {
        return '<span class="tok-shell">' + esc(line) + "</span>";
      }
      return '<span class="t-out">' + esc(line) + "</span>";
    }).join("\n");
  }

  function apply() {
    var blocks = document.querySelectorAll("code.lang-sb");
    for (var k = 0; k < blocks.length; k++) {
      var b = blocks[k];
      if (b.dataset.src) continue; // animated terminal handled by main.js
      var src = b.textContent;
      b.innerHTML = highlightSB(src);
    }
    var sh = document.querySelectorAll("code.lang-sh");
    for (var j = 0; j < sh.length; j++) {
      sh[j].innerHTML = highlightSH(sh[j].textContent);
    }
  }

  window.SUBHighlight = { highlightSB: highlightSB, highlightSH: highlightSH };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", apply);
  } else {
    apply();
  }
})();
