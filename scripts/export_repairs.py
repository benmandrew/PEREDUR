#!/usr/bin/env python3
"""Export a campaign's FRETISH repairs as a bundle a person can read.

    python3 scripts/export_repairs.py <results-dir> --out BUNDLE [--subjects ...]

For each subject (default: the cores in `make_core_specs.py`) it reads every
run directory, taking `repair_*.json` from a finished run and the files
`accumulated/maximal.tsv` names from one killed before writing `run.json`.
Under the implication filter that is the run's maximal set: no other repair
*that run* found implies one of them. Each run's `run.json` says whether the
filter was on, and the bundle words itself to match. Runs are not filtered
against each other. Repairs found by several runs are merged, keeping a record
of every run that found them.

The original specification is read at the commit the runs recorded, not from
the working tree, so an example edited since the campaign cannot be diffed
against by mistake. Pass `--commit` when no run wrote `run.json`.

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
import subprocess
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

def run_sources(run_dir: Path) -> dict:
    """Return a run's repair files and what the run recorded about them.

    A finished run's `repair_N.json` files are its maximal set only if the
    run had the implication filter on; otherwise they are everything that
    passed its output gate. A run killed before writing `run.json` keeps its
    streaming maximal set in `accumulated/maximal.tsv`, which exists only
    under the filter.
    """
    manifest = run_dir / "run.json"
    if manifest.exists():
        data = json.loads(manifest.read_text())
        return {"files": sorted(run_dir.glob("repair_*.json")),
                "censored": False, "commit": data.get("commit"),
                "dirty": bool(data.get("dirty")),
                "maximal": data.get("config", {}).get("filters", {})
                .get("run_implication")}
    index = run_dir / "accumulated" / "maximal.tsv"
    names = index.read_text().split("\n")[1:] if index.exists() else []
    return {"files": [run_dir / "accumulated" / n for n in names if n.strip()],
            "censored": True, "commit": None, "dirty": False,
            "maximal": True if index.exists() else None}


def runs_by_subject(results: Path, subjects: list[str]) -> dict:
    runs = defaultdict(list)
    for run_dir in sorted(p for p in results.iterdir() if p.is_dir()):
        match = RUN_DIR.match(run_dir.name)
        if match and match["subject"] in subjects:
            runs[match["subject"]].append(
                (run_dir, match, run_sources(run_dir)))
    missing = [s for s in subjects if s not in runs]
    if missing:
        sys.exit(f"no run directories for: {' '.join(missing)}")
    return runs


def campaign_commit(runs: dict, override: str | None) -> str:
    """Return the one commit every run recorded, or `override`.

    The original specification is read at this commit, because an example
    edited since the campaign would otherwise be diffed against silently.
    """
    recorded = {s["commit"] for subject in runs.values()
                for _, _, s in subject if s["commit"]}
    if any(s["dirty"] for subject in runs.values() for _, _, s in subject):
        print("warning: some runs were built from a dirty tree, so the "
              "examples at their commit may not be what they ran",
              file=sys.stderr)
    if override:
        if recorded and recorded != {resolve(override)}:
            print(f"warning: --commit {override} overrides the recorded "
                  f"{', '.join(sorted(recorded))}", file=sys.stderr)
        return resolve(override)
    if len(recorded) > 1:
        sys.exit(f"runs record different commits ({', '.join(sorted(recorded))}); "
                 "export them separately or pass --commit")
    if not recorded:
        sys.exit("no run recorded its commit (every run was killed before "
                 "writing run.json); pass --commit")
    return recorded.pop()


def resolve(commit: str) -> str:
    return subprocess.run(["git", "-C", str(REPO_ROOT), "rev-parse", commit],
                          check=True, capture_output=True,
                          text=True).stdout.strip()


def git_json(commit: str, path: str):
    result = subprocess.run(["git", "-C", str(REPO_ROOT), "show",
                             f"{commit}:{path}"], capture_output=True,
                            text=True)
    return json.loads(result.stdout) if result.returncode == 0 else None


def collect(runs: list) -> list[dict]:
    """Merge the runs' repairs, one entry per distinct specification."""
    found = {}
    for run_dir, match, source in runs:
        for path in source["files"]:
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
                "censored": source["censored"],
                "maximal_in_run": source["maximal"]})
    return list(found.values())


# --- The original and its parent ---

def subject_context(subject: str, commit: str) -> dict:
    """Read the subject at `commit` and work out where its guarantees came from.

    A core's guarantees are labelled with their parent indices only when the
    parent at the same commit reproduces them, so a stale `CORES` row cannot
    mislabel anything. FRET ids come from `reqids.json` at that commit, or from
    the working tree's when the parent is unchanged since.
    """
    raw = git_json(commit, f"examples/{subject}/spec.json")
    if raw is None:
        sys.exit(f"examples/{subject}/spec.json does not exist at {commit[:7]}")
    context = {"subject": subject, "raw": raw, "parent": None,
               "indices": None,
               "fret": {part: [{}] * len(raw[part])
                        for part in ("assumptions", "guarantees")}}
    parent, core = CORES.get(subject, (subject, None))
    parent_spec = raw if core is None else git_json(
        commit, f"examples/{parent}/spec.json")
    if core is not None:
        if parent_spec is None:
            print(f"warning: {subject}: parent {parent} missing at "
                  f"{commit[:7]}; no parent labels", file=sys.stderr)
            return context
        guarantees = parent_spec["guarantees"]
        indices = sorted(set(core) | {i for i, g in enumerate(guarantees)
                                      if g.get("weakenable") is False})
        if ([guarantees[i] for i in indices] != raw["guarantees"]
                or parent_spec["assumptions"] != raw["assumptions"]):
            print(f"warning: {subject} is not guarantees {core} of {parent} "
                  f"at {commit[:7]}; no parent labels", file=sys.stderr)
            return context
    else:
        indices = list(range(len(raw["guarantees"])))
    context.update(parent=parent, indices=indices)

    reqids = git_json(commit, f"examples/{parent}/reqids.json")
    if reqids is None:
        here = EXAMPLES / parent
        if (here / "reqids.json").exists() and json.loads(
                (here / "spec.json").read_text()) == parent_spec:
            reqids = json.loads((here / "reqids.json").read_text())
    if reqids:
        # A core keeps every assumption of its parent, in order.
        for part, key, wanted in (
                ("guarantees", "guarantee", indices),
                ("assumptions", "assumption",
                 range(len(raw["assumptions"])))):
            rows = {row[key]: row for row in reqids if key in row}
            context["fret"][part] = [rows.get(i, {}) for i in wanted]
    return context


def label_requirements(context: dict) -> dict[str, list[str]]:
    raw = context["raw"]
    labels = {"assumptions": [], "guarantees": []}
    for i in range(len(raw["assumptions"])):
        tags = context["fret"]["assumptions"][i].get("reqids", [])
        labels["assumptions"].append(
            f"A{i + 1} ({', '.join(tags)})" if tags else f"A{i + 1}")
    for j in range(len(raw["guarantees"])):
        tags = context["fret"]["guarantees"][j].get("reqids", [])
        if context["indices"] is not None:
            tags = [f"{context['parent']} #{context['indices'][j]}"] + tags
        labels["guarantees"].append(
            f"G{j + 1} ({', '.join(tags)})" if tags else f"G{j + 1}")
    return labels


def short_arms(arms: list[str]) -> dict[str, str]:
    """Name each arm by the underscore tokens that differ between arms."""
    split = {a: a.split("_") for a in arms}
    if len(arms) < 2 or len({len(t) for t in split.values()}) != 1:
        return {a: a for a in arms}
    varying = [k for k in range(len(next(iter(split.values()))))
               if len({t[k] for t in split.values()}) > 1]
    return {a: "_".join(t[k] for k in varying) for a, t in split.items()}


def render_core(context: dict, spec: dict, labels: dict,
                commit: str) -> str:
    subject, parent = context["subject"], context["parent"]
    lines = [f"# {subject}: original specification", "",
             f"`examples/{subject}/spec.json` at {commit[:7]}, the commit the "
             "runs were built from.", ""]
    if subject in CORES and context["indices"] is not None:
        lines += [f"An unrealisable core of `{parent}`: its guarantees "
                  f"{CORES[subject][1]} (0-based), plus every guarantee "
                  "locked against weakening, which the search may not "
                  "change.", ""]
    lines += ["`C` stands for the component. `[locked]` marks a requirement "
              "the search may not weaken.", ""]
    sources = context["fret"]
    if any(sources["guarantees"]) or any(sources["assumptions"]):
        lines += ["Under each requirement is the FRET sentence it came "
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


def maximality_note(runs: list) -> tuple[str, bool]:
    """Say what a subject's repair files are, from what its runs recorded."""
    flags = {s["maximal"] for _, _, s in runs if s["files"]}
    if flags <= {True}:
        return ("Each is maximal within a run that found it: no other repair "
                "from that run implies it.", True)
    if None in flags:
        return ("Some runs did not record whether they filtered by "
                "implication, so a repair may be implied by another from its "
                "own run.", False)
    return ("Some runs did not filter by implication, so a repair may be "
            "implied by another from its own run.", False)


def render_subject(subject: str, runs: list, out: Path, commit: str) -> tuple:
    repairs = collect(runs)
    context = subject_context(subject, commit)
    original = normalise_spec(context["raw"])
    labels = label_requirements(context)
    arms = short_arms(sorted({m["arm"] for _, m, _ in runs}))
    if out.exists():
        shutil.rmtree(out)
    (out / "repairs").mkdir(parents=True)
    (out / "core.json").write_text(json.dumps(context["raw"], indent=2) + "\n")
    (out / "core.md").write_text(
        render_core(context, original, labels, commit))
    note, maximal = maximality_note(runs)

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
          f"{len(runs)} runs. {note} Each is shown as its changes to `core.md`, closest to the "
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
    return subject, len(runs), n_found, len(repairs), maximal


def bundle_readme(rows) -> str:
    lines = [
        "# FRETISH repairs", "",
        "One folder per specification:", "",
        "- `core.md`: the original, as FRETISH text. Guarantee `G<n>` is "
        "labelled with its 0-based index in the parent specification and any "
        "FRET requirement ids.",
        "- `core.json`: the original, as PEREDUR reads it.",
        "- `repairs.md`: each repair as its changes to the original, most "
        "similar first (by fitness; unscored repairs last).",
        "- `repairs.csv`: one row per repair.",
        "- `repairs/`: each repair as JSON, with the runs that found it.", "",
        "Each repair is maximal within its run: no other repair from that run "
        "implies it. Runs are not filtered against each other."
        if all(row[4] for row in rows) else
        "Not every run filtered its repairs by implication; each "
        "`repairs.md` says which applies. Runs are not filtered against each "
        "other.", "",
        "Requirements read `[scope] [upon|whenever condition] C shall timing "
        "satisfy response`. `upon` fires when the condition becomes true, "
        "`whenever` at every step it holds. `[locked]` requirements may not "
        "be weakened.", "",
        "| Specification | Runs with a repair | Repairs |",
        "|---|---:|---:|"]
    for subject, n_runs, n_found, n_repairs, _ in rows:
        lines.append(f"| {subject} | {n_found} of {n_runs} | {n_repairs} |")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("results", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--subjects", nargs="+")
    parser.add_argument("--commit", help="read the original specifications "
                        "at this commit rather than the one the runs "
                        "recorded; needed when no run wrote run.json")
    args = parser.parse_args()
    subjects = args.subjects or list(CORES)
    runs = runs_by_subject(args.results, subjects)
    commit = campaign_commit(runs, args.commit)
    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for subject in subjects:
        rows.append(render_subject(subject, runs[subject],
                                   args.out / subject, commit))
        print(f"{subject}: {rows[-1][3]} repairs from {rows[-1][1]} runs")
    (args.out / "README.md").write_text(bundle_readme(rows))


if __name__ == "__main__":
    main()
