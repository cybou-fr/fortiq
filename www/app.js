"use strict";

const filters = document.querySelectorAll("[data-filter]");
const tools = document.querySelectorAll("[data-category]");
const counter = document.querySelector(".tools-count");
const pluralRules = new Intl.PluralRules(document.documentElement.lang);

filters.forEach((button) => {
  button.addEventListener("click", () => {
    const category = button.dataset.filter;
    let visible = 0;
    filters.forEach((item) => item.setAttribute("aria-pressed", String(item === button)));
    tools.forEach((tool) => {
      tool.hidden = category !== "all" && !tool.dataset.category.split(" ").includes(category);
      if (!tool.hidden) visible += 1;
    });
    const rule = pluralRules.select(visible);
    const word = rule === "one" ? counter.dataset.singular
      : rule === "few" ? counter.dataset.few : counter.dataset.many;
    counter.textContent = `${visible} ${word}`;
  });
});

const modes = document.querySelectorAll("[data-mode]");
const desktop = document.querySelector(".desktop");
modes.forEach((button) => {
  button.addEventListener("click", () => {
    desktop.dataset.scene = button.dataset.mode;
    modes.forEach((item) => item.setAttribute("aria-pressed", String(item === button)));
  });
});
