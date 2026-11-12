/* SUB site — interactivity: nav, tabs, copy, animated terminal. */
(function () {
  "use strict";

  /* ── Sticky nav border ── */
  var nav = document.querySelector(".nav");
  function onScroll() {
    nav.classList.toggle("scrolled", window.scrollY > 12);
  }
  window.addEventListener("scroll", onScroll, { passive: true });
  onScroll();

  /* ── Mobile nav ── */
  var burger = document.getElementById("navBurger");
  var links = document.getElementById("navLinks");
  if (burger && links) {
    burger.addEventListener("click", function () {
      links.classList.toggle("open");
    });
    links.addEventListener("click", function (e) {
      if (e.target.tagName === "A") links.classList.remove("open");
    });
  }

  /* ── Copy buttons ── */
  document.querySelectorAll(".copy-btn").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var text = btn.getAttribute("data-copy");
      if (!text) {
        var block = btn.closest(".code-block");
        if (block) {
          var code = block.querySelector("code");
          text = code ? code.textContent : "";
        }
      }
      if (!text) return;
      var done = function () {
        btn.classList.add("ok");
        btn.textContent = "✓";
        setTimeout(function () {
          btn.classList.remove("ok");
          btn.textContent = "⧉";
        }, 1600);
      };
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(done).catch(function () {});
      } else {
        var ta = document.createElement("textarea");
        ta.value = text;
        document.body.appendChild(ta);
        ta.select();
        try { document.execCommand("copy"); done(); } catch (e) {}
        document.body.removeChild(ta);
      }
    });
  });

  /* ── Code tabs ── */
  var tabs = document.querySelectorAll("#codeTabs .tab");
  var panels = document.querySelectorAll("#codeTabs .tab-panel");
  tabs.forEach(function (tab) {
    tab.addEventListener("click", function () {
      tabs.forEach(function (t) { t.classList.remove("active"); });
      panels.forEach(function (p) { p.classList.remove("active"); });
      tab.classList.add("active");
      var sel = '[data-panel="' + tab.dataset.tab + '"]';
      var panel = document.querySelector(sel);
      if (panel) panel.classList.add("active");
    });
  });

  /* ── Animated terminal ── */
  var term = document.querySelector('code[data-anim="type"]');
  if (term && !window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    var srcEl = document.getElementById("term-src");
    var full = srcEl ? srcEl.textContent.replace(/^\n+|\s+$/g, "") : "";
    term.textContent = "";
    var idx = 0;
    var cursor = document.createElement("span");
    cursor.className = "t-cursor";
    cursor.textContent = "▋";
    cursor.style.color = "#2fd4c4";
    cursor.style.animation = "blink 1s steps(1) infinite";

    function render() {
      var shown = full.slice(0, idx);
      // highlight $ lines
      var html = shown.split("\n").map(function (line) {
        if (line.indexOf("$") === 0) {
          return '<span class="t-prompt">' + esc(line.slice(0, 1)) + '</span><span class="t-cmd">' + esc(line.slice(1)) + "</span>";
        }
        var cls = /SUB Language/.test(line) ? "t-logo" : "t-out";
        return '<span class="' + cls + '">' + esc(line) + "</span>";
      }).join("\n");
      term.innerHTML = html;
      term.appendChild(cursor);
    }
    function esc(s) {
      return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }
    function typeStep() {
      if (idx >= full.length) return;
      idx++;
      render();
      var ch = full[idx - 1];
      var delay = 24;
      if (ch === "\n") delay = 260;
      else if (/[│┌└]/.test(ch)) delay = 8;
      setTimeout(typeStep, delay);
    }
    var blinkStyle = document.createElement("style");
    blinkStyle.textContent = "@keyframes blink { 50% { opacity: 0; } }";
    document.head.appendChild(blinkStyle);
    setTimeout(typeStep, 700);
  } else if (term) {
    // no animation: render statically
    var srcNode = document.getElementById("term-src");
    if (srcNode) {
      term.textContent = srcNode.textContent.replace(/^\n+|\s+$/g, "");
      term.innerHTML = term.textContent.split("\n").map(function (line) {
        if (line.indexOf("$") === 0) return '<span class="t-prompt">$</span><span class="t-cmd">' + line.slice(1) + "</span>";
        return '<span class="t-out">' + line + "</span>";
      }).join("\n");
    }
  }
})();
