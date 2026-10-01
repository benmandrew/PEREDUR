#!/usr/bin/env python3
"""Export a campaign's FRETISH repairs as a bundle a person can read.

    python3 scripts/export_repairs.py <results-dir> --out BUNDLE [--subjects ...]

For each subject (default: the cores in `make_core_specs.py`) it reads every
run directory, taking `repair_*.json` from a finished run and the files
`accumulated/maximal.tsv` names from one killed before writing `run.json`.
Either way that is the run's maximal set: no other repair *that run* found
implies one of them. Runs are not filtered against each other, so a repair
from one seed may be implied by a repair from another. Repairs found by
several runs are merged, keeping a record of every run that found them.

The bundle holds, per subject, the original specification as FRETISH text with
each guarantee's index in its parent and its FRET requirement ids where
`examples/<parent>/reqids.json` exists, then every repair as a diff against
it, with the full JSON alongside.
"""

import argparse
import csv
import difflib
import json
import re
import shutil
import sys
from collections import Counter, defaultdict
from pathlib import Path

from make_core_specs import CORES

REPO_ROOT = Path(__file__).resolve().parent.parent
EXAMPLES = REPO_ROOT / "examples"

# Run directories end in `_<subject>_seedNN` (gen_configs.py's naming); the
# rest of the name is the factor cross, kept whole as the run's arm.
RUN_DIR = re.compile(r"^(?P<arm>.+)_(?P<subject>[^_]+)_seed(?P<seed>\d+)$")

SPEC_FIELDS = ("assumptions", "guarantees", "in_atoms", "out_atoms", "modes")
REQ_FIELDS = ("scope", "condition-type", "condition", "timing", "response",
              "weakenable")


# --- Formulas ---
#
# The C++ printer parenthesises every operand (src/prop_formula/render.cpp),
# while an imported example keeps its author's spelling, so one formula
# arrives written two ways. Both are parsed with src/prop_formula/parser.cpp's
# grammar and printed with only the parentheses a reader needs.

LEVEL = {"<->": 1, "->": 2, "|": 3, "&": 4, "U": 5, "R": 5, "W": 5}
FORMULA_TOKEN = re.compile(r"\s*(<->|->|[!~&|()]|[A-Za-z_][A-Za-z0-9_]*)")


def parse_formula(text: str):
    """Return a tree: an atom name, (unary op, child) or (op, [children])."""
    tokens, pos = [], 0
    while text[pos:].strip():
        match = FORMULA_TOKEN.match(text, pos)
        if not match:
            raise ValueError(f"cannot read formula: {text!r}")
        tokens.append(match[1])
        pos = match.end()
    tokens.append(None)
    index = 0

    def take(*expected):
        nonlocal index
        if tokens[index] not in expected:
            return None
        index += 1
        return tokens[index - 1]

    def binary(level):
        if level > 5:
            return unary()
        lhs = binary(level + 1)
        if level == 2:  # `->` is right-associative.
            return ("->", [lhs, binary(2)]) if take("->") else lhs
        ops = [op for op, lvl in LEVEL.items() if lvl == level]
        while (op := take(*ops)):
            rhs = binary(level + 1)
            # & and | are associative, so a chain is one n-ary node.
            if op in ("&", "|") and isinstance(lhs, tuple) and lhs[0] == op:
                lhs = (op, lhs[1] + [rhs])
            else:
                lhs = (op, [lhs, rhs])
        return lhs

    def unary():
        if take("!", "~"):
            return ("!", unary())
        if (op := take("X", "F", "G")):
            return (op, unary())
        if take("("):
            inner = binary(1)
            if not take(")"):
                raise ValueError(f"unbalanced formula: {text!r}")
            return inner
        token = tokens[index]
        if token is None or not re.match(r"[A-Za-z_]", token):
            raise ValueError(f"cannot read formula: {text!r}")
        return take(token)

    tree = binary(1)
    if tokens[index] is not None:
        raise ValueError(f"trailing text in formula: {text!r}")
    return tree


def formula_text(tree, parent=None) -> str:
    """Print `tree`, bracketing a binary operand unless it binds tighter.

    Same-level operands are bracketed too, and `&` under `|`, which a reader
    should not have to resolve by precedence.
    """
    if isinstance(tree, str):
        return tree
    op, child = tree
    if not isinstance(child, list):
        inner = formula_text(child, op)
        return f"{op}{inner}" if op == "!" or inner[0] == "(" else f"{op} {inner}"
    text = f" {op} ".join(formula_text(c, op) for c in child)
    bracket = parent is not None and (
        parent not in LEVEL or LEVEL[op] <= LEVEL[parent]
        or (op, parent) == ("&", "|"))
    return f"({text})" if bracket else text


def normalise(text: str) -> str:
    return formula_text(parse_formula(text))


def normalise_requirement(req: dict) -> dict:
    out = {**req, "condition": normalise(req["condition"]),
           "response": normalise(req["response"])}
    if "stop" in req["timing"]:
        out["timing"] = {**req["timing"],
                         "stop": normalise(req["timing"]["stop"])}
    return out


def normalise_spec(spec: dict) -> dict:
    return {**spec, **{part: [normalise_requirement(r) for r in spec[part]]
                       for part in ("assumptions", "guarantees")}}


# --- FRETISH text, mirroring Requirement::to_string in src/requirement.cpp ---

SCOPE_WORDS = {"In": "in", "NotIn": "except in", "Before": "before",
               "After": "after", "OnlyIn": "only in",
               "OnlyBefore": "only before", "OnlyAfter": "only after"}


def timing_text(timing: dict) -> str:
    kind = timing["type"]
    fixed = {"Immediately": "immediately",
             "NextTimepoint": "at the next timepoint",
             "Eventually": "eventually", "Always": "always", "Never": "never"}
    if kind in fixed:
        return fixed[kind]
    if kind in ("WithinTicks", "ForTicks", "AfterTicks"):
        word = kind[:-len("Ticks")].lower()
        return f"{word} {timing['ticks']} ticks"
    if kind in ("Until", "Before"):
        return f"{kind.lower()} {timing['stop']}"
    raise ValueError(f"unknown timing type: {kind}")


def requirement_text(req: dict) -> str:
    parts = []
    scope = req.get("scope")
    if scope and scope["type"] != "Global":
        parts.append(f"{SCOPE_WORDS[scope['type']]} {scope['mode']}")
    trigger = req["condition-type"] == "trigger"
    if not (trigger and req["condition"] == "true"):
        parts.append(f"{'upon' if trigger else 'whenever'} {req['condition']}")
    parts.append(f"C shall {timing_text(req['timing'])} satisfy "
                 f"{req['response']}")
    text = " ".join(parts)
    return text + ("  [locked]" if req.get("weakenable") is False else "")


def changed_fields(old: dict, new: dict) -> list[str]:
    return [f for f in REQ_FIELDS if old.get(f) != new.get(f)]


# --- Requirement alignment ---

def canonical(obj) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))


def align(original: list[dict], repaired: list[dict]):
    """Yield (kind, original index or None, repaired requirement or None).

    A repair keeps its requirements in slot order and drops removed ones, so a
    longest-common-subsequence alignment pairs every untouched requirement with
    itself; a replaced run pairs position by position, its excess being
    removals or additions.
    """
    matcher = difflib.SequenceMatcher(
        a=[canonical(r) for r in original], b=[canonical(r) for r in repaired],
        autojunk=False)
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            for i, j in zip(range(i1, i2), range(j1, j2)):
                yield "unchanged", i, repaired[j]
            continue
        n_pairs = min(i2 - i1, j2 - j1) if tag == "replace" else 0
        for k in range(n_pairs):
            yield "changed", i1 + k, repaired[j1 + k]
        for i in range(i1 + n_pairs, i2):
            yield "removed", i, None
        for j in range(j1 + n_pairs, j2):
            yield "added", None, repaired[j]


# --- Collection ---

def run_sources(run_dir: Path) -> tuple[list[Path], bool]:
    """Return a run's maximal files, and whether the run was killed early."""
    if (run_dir / "run.json").exists():
        return sorted(run_dir.glob("repair_*.json")), False
    index = run_dir / "accumulated" / "maximal.tsv"
    if not index.exists():
        return [], True
    names = index.read_text().split("\n")[1:]
    return [run_dir / "accumulated" / n for n in names if n.strip()], True


def runs_by_subject(results: Path, subjects: list[str]) -> dict:
    runs = defaultdict(list)
    for run_dir in sorted(p for p in results.iterdir() if p.is_dir()):
        match = RUN_DIR.match(run_dir.name)
        if match and match["subject"] in subjects:
            runs[match["subject"]].append((run_dir, match))
    missing = [s for s in subjects if s not in runs]
    if missing:
        sys.exit(f"no run directories for: {' '.join(missing)}")
    return runs


def collect(runs: list) -> list[dict]:
    """Merge the runs' repairs, one entry per distinct specification."""
    found = {}
    for run_dir, match in runs:
        files, censored = run_sources(run_dir)
        for path in files:
            data = json.loads(path.read_text())
            spec = normalise_spec(
                {k: data[k] for k in SPEC_FIELDS if k in data})
            key = canonical({"assumptions": spec["assumptions"],
                             "guarantees": spec["guarantees"]})
            entry = found.setdefault(key, {"spec": spec, "found_by": []})
            entry["found_by"].append({
                "run": run_dir.name, "arm": match["arm"],
                "seed": int(match["seed"]), "file": path.name,
                "fitness": data.get("fitness", {}).get("total"),
                "censored": censored})
    return list(found.values())


# --- render ---

def parent_of(subject: str):
    """Return (parent name, parent index of each subject guarantee)."""
    if subject not in CORES:
        n = len(json.loads((EXAMPLES / subject / "spec.json").read_text())
                ["guarantees"])
        return subject, list(range(n))
    parent, core = CORES[subject]
    guarantees = json.loads(
        (EXAMPLES / parent / "spec.json").read_text())["guarantees"]
    keep = set(core) | {i for i, g in enumerate(guarantees)
                        if g.get("weakenable") is False}
    return parent, sorted(keep)


def fret_sources(subject: str) -> list[dict]:
    """Return the FRET source row of each subject guarantee, or {} if none."""
    parent, indices = parent_of(subject)
    path = EXAMPLES / parent / "reqids.json"
    rows = ({row["guarantee"]: row for row in json.loads(path.read_text())}
            if path.exists() else {})
    return [rows.get(i, {}) for i in indices]


def label_requirements(subject: str, spec: dict) -> dict[str, list[str]]:
    parent, indices = parent_of(subject)
    labels = {"assumptions": [f"A{i + 1}" for i in
                              range(len(spec["assumptions"]))],
              "guarantees": []}
    for j, (parent_index, source) in enumerate(
            zip(indices, fret_sources(subject))):
        tag = ", ".join([f"{parent} #{parent_index}"] +
                        source.get("reqids", []))
        labels["guarantees"].append(f"G{j + 1} ({tag})")
    return labels


def short_arms(arms: list[str]) -> dict[str, str]:
    """Name each arm by the underscore tokens that differ between arms."""
    split = {a: a.split("_") for a in arms}
    if len(arms) < 2 or len({len(t) for t in split.values()}) != 1:
        return {a: a for a in arms}
    varying = [k for k in range(len(next(iter(split.values()))))
               if len({t[k] for t in split.values()}) > 1]
    return {a: "_".join(t[k] for k in varying) for a, t in split.items()}


def render_core(subject: str, spec: dict, labels: dict) -> str:
    parent, _ = parent_of(subject)
    lines = [f"# {subject}: original specification", ""]
    if subject in CORES:
        lines += [f"An unrealisable core of `{parent}`: the guarantees "
                  f"{CORES[subject][1]} (0-based) of `examples/{parent}/"
                  "spec.json`, plus every guarantee locked against weakening, "
                  "which the search may not change.", ""]
    lines += ["`C` stands for the component. `[locked]` marks a requirement "
              "the search may not weaken.", ""]
    sources = {"assumptions": [{}] * len(spec["assumptions"]),
               "guarantees": fret_sources(subject)}
    if any(sources["guarantees"]):
        lines += ["Under each guarantee is the FRET sentence it was imported "
                  "from. Where the two differ, the import changed it; "
                  "`examples/<parent>`'s git history says how.", ""]
    for part in ("assumptions", "guarantees"):
        if not spec[part]:
            continue
        lines += [f"## {part.capitalize()}", ""]
        for label, req, source in zip(labels[part], spec[part],
                                      sources[part]):
            lines.append(f"- **{label}** `{requirement_text(req)}`")
            for reqid, text in source.get("fulltext", {}).items():
                lines.append(f"  - FRET {reqid}: {text}")
        lines.append("")
    if spec.get("modes"):
        lines += ["## Modes", "", ", ".join(f"`{m}`" for m in spec["modes"]),
                  ""]
    used = atoms_used(spec)
    for side in ("in_atoms", "out_atoms"):
        names = [a for a in spec[side] if a in used]
        title = "Inputs" if side == "in_atoms" else "Outputs"
        lines += [f"## {title} used", "",
                  ", ".join(f"`{a}`" for a in names) or "none", ""]
    lines.append(f"The full alphabet is in `core.json`.")
    return "\n".join(lines) + "\n"


def atoms_used(spec: dict) -> set[str]:
    text = " ".join(canonical(r) for part in ("assumptions", "guarantees")
                    for r in spec[part])
    return set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text))


def index_ranges(prefix: str, indices: list[int]) -> str:
    """Spell 0-based indices as 1-based label ranges: `G1–G3, G5`."""
    spans = []
    for i in indices:
        if spans and spans[-1][1] == i - 1:
            spans[-1][1] = i
        else:
            spans.append([i, i])
    return ", ".join(f"{prefix}{a + 1}" if a == b else
                     f"{prefix}{a + 1}–{prefix}{b + 1}" for a, b in spans)


def diff_lines(original: dict, repaired: dict, labels: dict) -> tuple:
    lines, counts = [], Counter()
    for part in ("assumptions", "guarantees"):
        unchanged = []
        for kind, i, req in align(original[part], repaired[part]):
            counts[kind] += 1
            if kind == "unchanged":
                unchanged.append(i)
            elif kind == "changed":
                assert i is not None and req is not None
                old = original[part][i]
                fields = ", ".join(changed_fields(old, req))
                lines += [f"- **{labels[part][i]}** changed ({fields})",
                          f"  - was: `{requirement_text(old)}`",
                          f"  - now: `{requirement_text(req)}`"]
            elif kind == "removed":
                assert i is not None
                lines += [f"- **{labels[part][i]}** removed",
                          f"  - was: `{requirement_text(original[part][i])}`"]
            else:
                assert req is not None
                noun = "assumption" if part == "assumptions" else "guarantee"
                lines += [f"- **new {noun}** `{requirement_text(req)}`"]
        if unchanged:
            prefix = part[0].upper()
            lines.append(f"- {part.capitalize()} unchanged: "
                         f"{index_ranges(prefix, unchanged)}")
    return lines, counts


def render_subject(subject: str, runs: list, out: Path) -> tuple:
    repairs = collect(runs)
    original = normalise_spec(
        json.loads((EXAMPLES / subject / "spec.json").read_text()))
    labels = label_requirements(subject, original)
    arms = short_arms(sorted({m["arm"] for _, m in runs}))
    if out.exists():
        shutil.rmtree(out)
    (out / "repairs").mkdir(parents=True)
    shutil.copy(EXAMPLES / subject / "spec.json", out / "core.json")
    (out / "core.md").write_text(render_core(subject, original, labels))

    for repair in repairs:
        fitness = [f["fitness"] for f in repair["found_by"]
                   if f["fitness"] is not None]
        repair["best_fitness"] = max(fitness) if fitness else None
        repair["runs"] = sorted({f["run"] for f in repair["found_by"]})
    # Every repair scores 1 on status, so fitness ranks the syntactic and
    # semantic closeness to the original.
    repairs.sort(key=lambda r: (-(r["best_fitness"] or 0), -len(r["runs"]),
                                canonical(r["spec"])))

    n_found = len({f["run"] for r in repairs for f in r["found_by"]})
    md = [f"# {subject}: repairs", "",
          f"{len(repairs)} distinct repairs, found by {n_found} of "
          f"{len(runs)} runs. Each is maximal within a run that found it. "
          "Each is shown as its changes to `core.md`, closest to the "
          "original first.", ""]
    if all(r["best_fitness"] is None for r in repairs):
        md[-2] += (" Every run was killed at its time limit before scoring "
                   "its repairs, so none carries a fitness and they are "
                   "ordered by how many runs found them.")
    csv_rows = []
    for n, repair in enumerate(repairs, start=1):
        name = f"r{n:04d}.json"
        (out / "repairs" / name).write_text(json.dumps(
            {**repair["spec"], "found_by": repair["found_by"]},
            indent=2) + "\n")
        by_arm = Counter(arms[f["arm"]] for f in repair["found_by"])
        seeds = sorted({f["seed"] for f in repair["found_by"]})
        fitness = repair["best_fitness"]
        arm_counts = ", ".join(f"{a} {c}" for a, c in sorted(by_arm.items()))
        md += [f"## r{n:04d}", "",
               f"Found by {len(repair['runs'])} "
               f"run{'s' if len(repair['runs']) > 1 else ''} ({arm_counts}); seeds "
               f"{', '.join(map(str, seeds))}; best fitness "
               f"{'n/a' if fitness is None else f'{fitness:.3f}'}. "
               f"File `repairs/{name}`.", ""]
        lines, counts = diff_lines(original, repair["spec"], labels)
        md += lines + [""]
        csv_rows.append({
            "repair": f"r{n:04d}", "runs": len(repair["runs"]),
            **{f"runs_{a}": by_arm.get(a, 0) for a in sorted(set(arms.values()))},
            "seeds": " ".join(map(str, seeds)),
            "best_fitness": "" if fitness is None else f"{fitness:.6f}",
            "changed": counts["changed"], "removed": counts["removed"],
            "added": counts["added"]})
    (out / "repairs.md").write_text("\n".join(md))
    with open(out / "repairs.csv", "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["repair", "runs"] + [
            f"runs_{a}" for a in sorted(set(arms.values()))] + [
            "seeds", "best_fitness", "changed", "removed", "added"])
        writer.writeheader()
        writer.writerows(csv_rows)
    return subject, len(runs), n_found, len(repairs)


def bundle_readme(rows) -> str:
    lines = [
        "# FRETISH repairs", "",
        "One folder per specification. Each holds:", "",
        "- `core.md`: the original specification as FRETISH text. Every "
        "guarantee is labelled `G<n>` with its index in the parent "
        "specification (0-based) and, where known, its FRET requirement ids.",
        "- `core.json`: the same specification as PEREDUR reads it.",
        "- `repairs.md`: every repair, as the requirements it changes, removes "
        "or adds, closest to the original first: ordered by the best fitness "
        "any run gave it, which for a repair measures syntactic and semantic "
        "similarity to the original. A repair from a run killed at its time "
        "limit has no fitness and sorts after those that do.",
        "- `repairs.csv`: one row per repair, for sorting and filtering.",
        "- `repairs/`: each repair as a whole specification in JSON, with "
        "`found_by` listing the runs that produced it.", "",
        "Each run keeps only its maximal repairs: those no other repair from "
        "the same run implies, so they give up the least. Runs are not "
        "filtered against each other, so a repair here may be implied by one "
        "another run found.", "",
        "Requirements read `[scope] [upon|whenever condition] C shall timing "
        "satisfy response`, where `C` is the component. `upon` fires on the "
        "condition's rising edge, `whenever` at every step it holds. "
        "`[locked]` marks a requirement the search may not weaken.", "",
        "| Specification | Runs | Runs that found a repair | Repairs |",
        "|---|---:|---:|---:|"]
    for subject, n_runs, n_found, n_repairs in rows:
        lines.append(f"| {subject} | {n_runs} | {n_found} | {n_repairs} |")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("results", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--subjects", nargs="+")
    args = parser.parse_args()
    subjects = args.subjects or list(CORES)
    runs = runs_by_subject(args.results, subjects)
    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for subject in subjects:
        rows.append(render_subject(subject, runs[subject],
                                   args.out / subject))
        print(f"{subject}: {rows[-1][3]} repairs from {rows[-1][1]} runs")
    (args.out / "README.md").write_text(bundle_readme(rows))


if __name__ == "__main__":
    main()
