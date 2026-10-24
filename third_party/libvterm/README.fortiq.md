# libvterm

Vendored `src/`, `include/`, `LICENSE` from https://github.com/neovim/libvterm
at commit `934bc2fbf21800ac3458a499df8820ca5fb45fd3` (22 October 2026).
MIT license; included in distributed third-party notices. Implements terminal state,
VT escape parsing and keyboard encoding. FORTIQ supplies SSH transport and a Qt view.

## Local patches (S-10, 25 October 2026)

Three defects found by `tests/TerminalSequencesTest.cpp` (hostile output from the server) are fixed here, each marked `FORTIQ` in the code.
Re-apply them (or confirm that the upstream commit has them) whenever this directory is updated from upstream:

1. `src/parser.c`, CSI parameters: more than `CSI_ARGS_MAX` (16) `;`-separated parameters wrote past `args[]` (crash with `ESC[1;1;1;...m`).
2. `src/parser.c`, digits: a parameter value was multiplied without a limit (signed overflow; `long` is 32-bit on Windows, so the value could run into `CSI_ARG_FLAG_MORE`); now saturates below 10^9.
3. `src/state.c`, REP (`ESC[<n>b`) with no previous glyph looped forever (`combine_width == 0`): one hostile line froze the GUI thread.
