/* FORTIQ site: progressive enhancement only (theme toggle, scroll reveal, pointer effects). Works without JavaScript. */
(function () {
  var root = document.documentElement, key = "fortiq-theme";
  var calm = matchMedia("(prefers-reduced-motion: reduce)").matches;
  root.classList.add("js");
  try { var saved = localStorage.getItem(key); if (saved === "light" || saved === "dark") root.dataset.theme = saved; } catch (e) {}

  function ready() {
    var btn = document.getElementById("theme-toggle");
    if (btn) {
      btn.hidden = false;
      btn.addEventListener("click", function () {
        var dark = root.dataset.theme ? root.dataset.theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
        root.classList.add("theming");
        root.dataset.theme = dark ? "light" : "dark";
        try { localStorage.setItem(key, root.dataset.theme); } catch (e) {}
        setTimeout(function () { root.classList.remove("theming"); }, 600);
      });
    }

    // Sticky header gets a firmer background once the page scrolls.
    var header = document.querySelector(".site-header");
    function onScroll() { if (header) header.classList.toggle("scrolled", scrollY > 8); }
    addEventListener("scroll", onScroll, { passive: true }); onScroll();

    // Scroll reveal.
    var items = document.querySelectorAll(".rv");
    if (calm || !("IntersectionObserver" in window)) {
      items.forEach(function (el) { el.classList.add("in"); });
    } else {
      var io = new IntersectionObserver(function (entries) {
        entries.forEach(function (en) {
          if (en.isIntersecting) { en.target.classList.add("in"); io.unobserve(en.target); }
        });
      }, { threshold: 0.12, rootMargin: "0px 0px -6% 0px" });
      items.forEach(function (el) { io.observe(el); });
    }

    if (calm || !matchMedia("(hover: hover) and (pointer: fine)").matches) return;

    // Light spot following the pointer on application tiles.
    document.querySelectorAll(".tile").forEach(function (t) {
      t.addEventListener("pointermove", function (ev) {
        var r = t.getBoundingClientRect();
        t.style.setProperty("--mx", ev.clientX - r.left + "px");
        t.style.setProperty("--my", ev.clientY - r.top + "px");
      });
    });

    // Gentle 3D tilt and sheen of the hero screenshot.
    var stage = document.querySelector(".stage"), frame = document.querySelector(".frame");
    if (stage && frame) {
      stage.addEventListener("pointermove", function (ev) {
        var r = frame.getBoundingClientRect();
        var x = (ev.clientX - r.left) / r.width, y = (ev.clientY - r.top) / r.height;
        frame.style.setProperty("--ry", ((x - 0.5) * 5).toFixed(2) + "deg");
        frame.style.setProperty("--rx", ((0.5 - y) * 4).toFixed(2) + "deg");
        frame.style.setProperty("--mx", x * 100 + "%");
        frame.style.setProperty("--my", y * 100 + "%");
        frame.style.setProperty("--sheen", "1");
      });
      stage.addEventListener("pointerleave", function () {
        frame.style.setProperty("--rx", "0deg"); frame.style.setProperty("--ry", "0deg");
        frame.style.setProperty("--sheen", "0");
      });
    }
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", ready); else ready();
})();
