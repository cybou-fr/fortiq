/* FORTIQ site: optional light/dark toggle only. The site works without JavaScript. */
(function () {
  var root = document.documentElement, key = "fortiq-theme";
  try { var saved = localStorage.getItem(key); if (saved === "light" || saved === "dark") root.dataset.theme = saved; } catch (e) {}
  document.addEventListener("DOMContentLoaded", function () {
    var btn = document.getElementById("theme-toggle");
    if (!btn) return;
    btn.hidden = false;
    btn.addEventListener("click", function () {
      var dark = root.dataset.theme ? root.dataset.theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
      root.dataset.theme = dark ? "light" : "dark";
      try { localStorage.setItem(key, root.dataset.theme); } catch (e) {}
    });
  });
})();
