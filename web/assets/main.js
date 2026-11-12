/* SUB site — nav toggle, copy buttons, code tabs. Static site, no gimmicks. */
(function () {
  "use strict";

  /* Mobile nav */
  var toggle = document.getElementById("navToggle");
  var nav = document.getElementById("siteNav");
  if (toggle && nav) {
    toggle.addEventListener("click", function () {
      var open = nav.classList.toggle("open");
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
    nav.addEventListener("click", function (e) {
      if (e.target.tagName === "A") {
        nav.classList.remove("open");
        toggle.setAttribute("aria-expanded", "false");
      }
    });
  }

  /* Copy buttons */
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
        var old = btn.textContent;
        btn.classList.add("ok");
        btn.textContent = "Copied";
        setTimeout(function () {
          btn.classList.remove("ok");
          btn.textContent = old;
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

  /* Code tabs */
  var tabs = document.querySelectorAll("#codeTabs .tab");
  var panels = document.querySelectorAll("#codeTabs .tab-panel");
  tabs.forEach(function (tab) {
    tab.addEventListener("click", function () {
      tabs.forEach(function (t) {
        t.classList.remove("active");
        t.setAttribute("aria-selected", "false");
      });
      panels.forEach(function (p) { p.classList.remove("active"); });
      tab.classList.add("active");
      tab.setAttribute("aria-selected", "true");
      var panel = document.querySelector('[data-panel="' + tab.dataset.tab + '"]');
      if (panel) panel.classList.add("active");
    });
  });
})();
