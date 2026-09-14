---
name: docs-build
description: Build the PEREDUR documentation site (Doxygen + Breathe + Sphinx). Use when running the docs target or debugging the docs build.
---

# Docs

```sh
cmake --build build --target docs   # Doxygen + Sphinx (requires both installed)
```

The `docs` target builds one site: the **curated public API** (`docs/Doxyfile.in` → XML → Breathe → Sphinx/furo, from `include/` only). Implementation detail under `src/` is deliberately not published — there is no internal reference build.
