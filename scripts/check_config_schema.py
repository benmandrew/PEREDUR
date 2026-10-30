#!/usr/bin/env python3
"""Check that ``DEFAULTS`` in ``scripts/gen_configs.py`` mirrors the binary.

``DEFAULTS`` is the harness's Python copy of the built-in defaults, and
``--pin-vintage`` writes it verbatim into generated configs, so a stale entry
is a wrong value baked into a campaign archive. ``example-config.toml`` is
generated from ``k_config_keys`` and every value in it is the built-in default
(the ``config_reference`` test holds it to that), so this compares each entry
against the template.
"""

import argparse
import importlib.util
import sys
import tomllib
from pathlib import Path

# Each gen_configs.DEFAULTS key is spelt as the last component of the TOML path
# it mirrors, and no two template keys share a last component, so the pairing
# is by name. A key in DEFAULTS that names no template key and is not in
# GEN_CONFIGS_EXEMPT fails the check: that is what stops the next drift, and it
# is also where a harness-only knob with no C++ counterpart has to be declared
# rather than silently skipped.

# Entries that deliberately do not track the binary, each with the reason it
# must not be "corrected" into agreement. The first is a results-CSV key
# column and the next three are emitted unconditionally, so following the
# binary would put every new row at odds with the archived ones; the
# last four are sentinels make_toml reads as "emit nothing", not mirrors of a
# C++ value at all.
GEN_CONFIGS_EXEMPT = {
    "selection_scheme":
        "pinned 'nsga2-apportion': the scheme is the config directory name read "
        "back into the `selection` KEY_FIELDS column",
    "weight_syntactic":
        "pinned 0.33 and emitted unconditionally, so every archived config "
        "states it; moving it breaks comparability with every past grid",
    "weight_semantic": "pinned 0.33 and emitted unconditionally (see "
                       "weight_syntactic)",
    "weight_status": "pinned 0.33 and emitted unconditionally (see "
                     "weight_syntactic)",
    "ltlsynt_timeout_ms":
        "0 is make_toml's 'emit only when positive' sentinel, not a mirror of "
        "the 10000 ms default",
    "ltl2tgba_timeout_ms":
        "0 is make_toml's 'emit only when positive' sentinel, not a mirror of "
        "the 60000 ms default",
    "max_scoring_failure_rate":
        "0.0 is make_toml's 'emit only when positive' sentinel, not a mirror of "
        "the 0.15 default",
    "parallel":
        "0 is make_toml's 'emit only when positive' sentinel; the default is "
        "available_parallelism(), a call rather than a constant, so there is "
        "nothing here to track",
}


def gen_configs_defaults(root):
    """Import gen_configs.py for its DEFAULTS table alone.

    Loaded by path rather than by name, so --root selects the checkout being
    checked. The module's top level is nothing but tables and function
    definitions, so importing it runs no campaign logic and writes no files.
    """
    path = root / "scripts" / "gen_configs.py"
    spec = importlib.util.spec_from_file_location("_gen_configs_for_check", path)
    if spec is None or spec.loader is None:
        raise ValueError(f"{path}: cannot be loaded as a module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return dict(module.DEFAULTS)


def leaves(table, prefix=""):
    """Map each template key's name to its dotted path and default."""
    out = {}
    for name, value in table.items():
        if isinstance(value, dict):
            out.update(leaves(value, f"{prefix}{name}."))
        else:
            out[name] = (prefix + name, value)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="repository root (default: the parent of scripts/)",
    )
    root = ap.parse_args().root
    try:
        defaults = gen_configs_defaults(root)
        example = leaves(
            tomllib.loads((root / "example-config.toml").read_text()))
    except (OSError, ValueError, AttributeError, ImportError) as exc:
        print(f"Config defaults check could not run: {exc}", file=sys.stderr)
        return 1

    errors = []
    for key in sorted(defaults):
        if key in GEN_CONFIGS_EXEMPT:
            continue
        if key not in example:
            errors.append(
                f"gen_configs.DEFAULTS['{key}'] names no key of "
                f"example-config.toml, so its value goes unchecked. Spell it as "
                f"the key it mirrors, or exempt it in GEN_CONFIGS_EXEMPT with "
                f"the reason it must not track the binary.")
            continue
        path, want = example[key]
        if want != defaults[key]:
            errors.append(
                f"gen_configs.DEFAULTS['{key}'] is {defaults[key]!r}, but the "
                f"binary defaults {path} to {want!r}. Correct it, or add "
                f"'{key}' to GEN_CONFIGS_EXEMPT with the reason it diverges.")

    # A stale exemption reads as a live one and would let a real divergence in
    # under an old key's name.
    for key in sorted(set(GEN_CONFIGS_EXEMPT) - set(defaults)):
        errors.append(f"'{key}' is exempted here but is not in "
                      f"gen_configs.DEFAULTS any more. Drop the stale entry.")

    if errors:
        print("Config defaults check failed:\n", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"Config defaults: {len(defaults) - len(GEN_CONFIGS_EXEMPT)} "
          f"gen_configs.DEFAULTS entries match the binary "
          f"({len(GEN_CONFIGS_EXEMPT)} exempt).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
