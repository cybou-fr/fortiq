'use strict';

const filters = document.querySelectorAll('[data-filter]');
const tools = document.querySelectorAll('[data-category]');
const counter = document.querySelector('.tools-count');

filters.forEach((button) => {
  button.addEventListener('click', () => {
    const category = button.dataset.filter;
    let visible = 0;
    filters.forEach((item) => {
      const selected = item === button;
      item.classList.toggle('active', selected);
      item.setAttribute('aria-pressed', String(selected));
    });
    tools.forEach((tool) => {
      const matches = category === 'all' || tool.dataset.category.split(' ').includes(category);
      tool.hidden = !matches;
      if (matches) visible += 1;
    });
    const plural = visible % 10 === 1 && visible % 100 !== 11 ? 'инструмент'
      : visible % 10 >= 2 && visible % 10 <= 4 && (visible % 100 < 12 || visible % 100 > 14)
        ? 'инструмента' : 'инструментов';
    counter.textContent = `${visible} ${plural}`;
  });
});
