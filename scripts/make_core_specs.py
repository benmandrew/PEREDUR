#!/usr/bin/env python3
"""Write each unrealisable core of the large FRETISH examples as its own example.

The whole specs are too large to repair monolithically, so a campaign repairs
each core as a separate spec. A core example is its parent with the guarantees
cut to the core's indices plus every non-weakenable guarantee (the background
the core was extracted against); assumptions and every other field are kept.
Indices are 0-based into the parent's `guarantees`.

    python3 scripts/make_core_specs.py          # write examples/<name>/spec.json
    python3 scripts/make_core_specs.py --check  # exit 1 if any is stale
"""
import argparse
import json
import sys
from pathlib import Path

EXAMPLES = Path(__file__).resolve().parent.parent / "examples"

# rad: the ten MUCs of examples/rad (ordinal ticks, 4ccc04e). lift-plus-cruise:
# deletion-based extraction against the speed-limit background (c8b3309), one
# core each.
CORES: dict[str, tuple[str, list[int]]] = {
    "rad-core-1-18": ("rad", [1, 18]),
    "rad-core-10": ("rad", [10]),
    "rad-core-12-18": ("rad", [12, 18]),
    "rad-core-17-18": ("rad", [17, 18]),
    "rad-core-33": ("rad", [33]),
    "rad-core-45": ("rad", [45]),
    "rad-core-55": ("rad", [55]),
    "rad-core-61": ("rad", [61]),
    "rad-core-65": ("rad", [65]),
    "rad-core-68": ("rad", [68]),
    "lpc-mini-core1": ("lift-plus-cruise-mini",
                       [12, 13, 34, 39, 40, 41, 44, 45]),
    "lpc-full-core1": ("lift-plus-cruise-full",
                       [16, 17, 18, 43, 45, 47, 48, 50, 51, 52, 55, 56]),
}


def core_spec(parent: str, core: list[int]) -> str:
    spec = json.loads((EXAMPLES / parent / "spec.json").read_text())
    guarantees = spec["guarantees"]
    keep = set(core) | {i for i, g in enumerate(guarantees)
                        if g.get("weakenable") is False}
    spec["guarantees"] = [guarantees[i] for i in sorted(keep)]
    return json.dumps(spec, indent=2) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    stale = []
    for name, (parent, core) in CORES.items():
        path = EXAMPLES / name / "spec.json"
        text = core_spec(parent, core)
        if args.check:
            if not path.exists() or path.read_text() != text:
                stale.append(name)
            continue
        path.parent.mkdir(exist_ok=True)
        path.write_text(text)
    if stale:
        sys.exit(f"stale core examples: {' '.join(stale)}")


if __name__ == "__main__":
    main()
