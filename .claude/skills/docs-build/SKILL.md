---
name: docs-build
description: Build the PEREDUR documentation site (Doxygen + Breathe + Sphinx). Use when running the docs target or debugging the docs build.
---

# Docs

```sh
nix develop
cmake --build build --target docs
open build/docs/sphinx/index.html
```

The site covers the public API only: Doxygen reads `include/` (`docs/Doxyfile.in`), Breathe passes its XML to Sphinx, and the theme is furo. `src/` is not published. The rules for writing the docs are in "Docs" in `docs/dev/tooling.md`; read it before changing a header comment or a `.rst` page.

## Common failures

- **`docs requires: doxygen, sphinx-build, katex`.** `cmake/docs.cmake` looks for the tools at configure time and installs a failing stub target when one is missing. The build tree was configured outside `nix develop`. Re-run `cmake --preset debug` inside the shell.
- **A Doxygen warning stops the build.** `WARN_AS_ERROR = YES`. A bare `<input>` in a `///` comment parses as an HTML tag; wrap it in backticks. Only `///` comments reach Doxygen.
- **A new header is missing from the site.** Every header in `include/` needs a page under `docs/api/` and an entry in the `docs/index.rst` toctree.
- **Maths renders as raw TeX or fetches from a CDN.** KaTeX's stylesheet and fonts are staged into `_static/katex/` by `cmake/docs.cmake`, and `docs/conf.py.in` points `katex_css_path` there. The two must share a directory.
