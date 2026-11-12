/* SUB site — platform detection + live latest-release asset links.
   The static hrefs point at a pinned release so the page works with JS
   off; dl.js upgrades every link to the actual latest release's asset by
   name pattern, so the site never goes stale when a new version ships. */
(function () {
  "use strict";
  var REPO = "subhobhai943/sub-lang";

  /* Which card is "this machine"? */
  function detectOS() {
    var ua = navigator.userAgent + " " + (navigator.platform || "");
    if (/Win(dows|32|64)/i.test(ua) || /WinMT/i.test(ua)) return "windows";
    if (/Mac/i.test(ua)) return "macos";
    if (/FreeBSD|BSD/i.test(ua)) return "freebsd";
    if (/Linux|X11/i.test(ua)) return "linux";
    return null;
  }
  var os = detectOS();
  var lead = document.getElementById("dlDetect");
  if (os) {
    var card = document.querySelector('.dl-card[data-os="' + os + '"]');
    if (card) card.classList.add("dl-hit");
  } else if (lead) {
    lead.textContent = "Pick your platform:";
  }

  /* Asset-name patterns per platform, main button first, alternates in
     DOM order. Each dl-card's links are matched against these in order. */
  var PATTERNS = {
    windows: [/windows.*x86_64\.msi$/i],
    macos:   [/macos-arm64\.pkg$/i, /sub-macos-arm64\.tar\.gz$/i],
    linux:   [/_amd64\.deb$/i, /_arm64\.deb$/i, /-1\.x86_64\.rpm$/i, /-1\.aarch64\.rpm$/i, /sub-linux-x86_64\.tar\.gz$/i],
    freebsd: [/sub-lang-.*-amd64\.pkg$/i, /sub-freebsd-x86_64\.tar\.gz$/i]
  };

  fetch("https://api.github.com/repos/" + REPO + "/releases/latest", {
    headers: { Accept: "application/vnd.github+json" }
  })
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (rel) {
      var ver = document.getElementById("dlVersion");
      if (ver && rel.tag_name) ver.textContent = rel.tag_name;

      var names = (rel.assets || []).map(function (a) { return a.name; });
      Object.keys(PATTERNS).forEach(function (plat) {
        var card = document.querySelector('.dl-card[data-os="' + plat + '"]');
        if (!card) return;
        var links = card.querySelectorAll("a");
        var pats = PATTERNS[plat];
        var used = 0;
        for (var i = 0; i < links.length && used < pats.length; i++) {
          var re = pats[used];
          var hit = names.filter(function (n) { return re.test(n); })[0];
          if (hit) {
            links[i].setAttribute("href",
              "https://github.com/" + REPO + "/releases/download/" + rel.tag_name + "/" +
              hit.replace(/ /g, "%20"));
            used++;
          }
        }
      });
    })
    .catch(function () { /* keep the pinned static links */ });
})();
