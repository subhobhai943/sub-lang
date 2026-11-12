/* SUB site — hero matrix rain + typing terminal demo. Respects reduced motion. */
(function () {
  "use strict";
  var reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  /* ── Matrix rain ── */
  var canvas = document.getElementById("matrixRain");
  if (canvas && canvas.getContext && !reduced) {
    var ctx = canvas.getContext("2d");
    var chars = "01{}()[]<>=+-*/fn let const class sub".split("");
    var fontSize = 14, cols = 0, drops = [];
    function resize() {
      var r = canvas.parentElement.getBoundingClientRect();
      canvas.width = r.width; canvas.height = r.height;
      cols = Math.floor(canvas.width / fontSize);
      drops = [];
      for (var i = 0; i < cols; i++) drops[i] = Math.random() * -40;
    }
    resize();
    window.addEventListener("resize", resize);
    var last = 0;
    function frame(t) {
      if (t - last > 60) {
        last = t;
        ctx.fillStyle = "rgba(10, 14, 18, 0.14)";
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        ctx.font = fontSize + "px monospace";
        for (var i = 0; i < cols; i++) {
          var ch = chars[(Math.random() * chars.length) | 0];
          ctx.fillStyle = Math.random() > 0.975 ? "#cfe8ff" : "#2e7dd7";
          ctx.fillText(ch, i * fontSize, drops[i] * fontSize);
          if (drops[i] * fontSize > canvas.height && Math.random() > 0.975) drops[i] = Math.random() * -20;
          drops[i]++;
        }
      }
      requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
  }

  /* ── Typing terminal ── */
  var term = document.getElementById("termType");
  if (!term) return;
  var lines = [
    ["$", "subc hello.sb -o hello"],
    [null, "Hello, World"],
    [null, "30"],
    ["$", "sub program.sb rust"],
    [null, "wrote program.rs (13 targets available)"]
  ];
  function esc(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;"); }
  function lineHtml(l, partial) {
    var text = partial == null ? l[1] : l[1].slice(0, partial);
    return l[0]
      ? '<span class="t-prompt">$ </span>' + esc(text)
      : '<span class="t-out">' + esc(text) + "</span>";
  }
  function render(done, partialLen) {
    var h = "";
    for (var i = 0; i < done && i < lines.length; i++) h += lineHtml(lines[i]) + "\n";
    if (done < lines.length && partialLen != null) h += lineHtml(lines[done], partialLen);
    term.innerHTML = h;
  }
  if (reduced) { render(lines.length, null); return; }

  var done = 0, typed = 0;
  function type() {
    if (done >= lines.length) {
      setTimeout(function () { done = 0; typed = 0; render(0, 0); setTimeout(type, 500); }, 4200);
      return;
    }
    var l = lines[done];
    if (typed < l[1].length) {
      typed++;
      render(done, typed);
      setTimeout(type, l[0] ? 34 + Math.random() * 50 : 16);
    } else {
      done++; typed = 0;
      render(done, done < lines.length ? 0 : null);
      setTimeout(type, l[0] ? 340 : 80);
    }
  }
  setTimeout(type, 700);
})();
