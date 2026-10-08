#!/usr/bin/env python3
"""Fingerprint pooled FRETISH repairs on drawn lasso words, then plan the pairs
that `compare` must decide.

    python3 scripts/fingerprint_members.py SUBJECTS.txt MEMBERS_DIR WORK_DIR OUT_DIR \\
        [--pairs PAIRS.csv] [--words 32768] [--seed 3] [--workers 8] \\
        [--black-timeout 30] [--manifest experiments/compare-manifests/NAME.json]

What a `kind = "fingerprint"` phase in campaign.toml runs. SUBJECTS.txt names
one subject a line. For each, MEMBERS_DIR/<subject>.jsonl.gz holds its pooled
nodes (experiments/2026-10-08-fretish-rerun-analysis/scripts/pool_members.py),
and the steps are those of the archive's draw (the local attempt's draw.sh,
after the frontier-approx batch5.sh):

1. Write each node to WORK_DIR/<subject>/nodes/<node>.json.
2. `fpdraw ltl examples/<subject>/spec.json` over the members, each node listed
   once per member that holds it, so a word's target is drawn by member.
3. draw_black.py: WORDS black lasso models at SEED, WORKERS in parallel.
4. `fpdraw eval` of every node on those words (FPDRAW_UNGLUE=1 on rad-core-*
   and mode-arbiter, as the archive), to OUT_DIR/<subject>/fp.tsv.
5. Plan: an unordered node pair is a candidate when some direction survives
   the words (a word that one node accepts and the other rejects refutes that
   direction). Every candidate is written to WORK_DIR/<subject>/pairs.csv as
   id,a_path,b_path,dirs, with dirs `both`, `fwd` (only a => b open) or `rev`
   (only b => a open), the COMPARE_DIRECTIONS value compare_pairs.py hands the
   patched compare.

OUT_DIR/<subject>/ keeps what the analysis reads and a collect should copy:
the words, their .meta, fp.tsv and the logs. WORK_DIR keeps the bulk only the
next phase reads: node files, lists, the LTL dump and the pair lists.

Each step's output is written whole and renamed into place, and a subject is
done once its pairs.csv exists, so a killed pass resumes at the first missing
step. A finished subject appends one row to OUT_DIR/done-<SUBJECTS stem>.csv.
With --pairs, the subjects' pair lists are joined into PAIRS.csv once every
subject is done. The exit status is 0 only then.

Stdlib only, on python 3.10: this runs on the lab hosts from a cron tick.
"""

import argparse
import csv
import gzip
import hashlib
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD = REPO_ROOT / "build-release"
THIRD_PARTY = BUILD / "third_party"
DRAW_SCRIPT = (REPO_ROOT / "experiments" / "2026-10-01-fretish-frontier-approx"
               / "scripts" / "draw_black.py")

# The values a fingerprint phase takes when campaign.toml omits a key.
# campaign.py reads them from here, so the declaration and the CLI agree.
DEFAULTS = {
    "words": 32768,
    "seed": 3,
    "workers": 8,
    "black_timeout": 30,
}

PAIRS_HEADER = ["id", "a_path", "b_path", "dirs"]


def log(*parts) -> None:
    print(time.strftime("%H:%M:%S"), *parts, flush=True)


def load_nodes(members_dir: str, subject: str) -> list:
    with gzip.open(os.path.join(members_dir, f"{subject}.jsonl.gz"), "rt") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def node_path(work: str, subject: str, node: int) -> str:
    return os.path.join(work, subject, "nodes", f"{node:05d}.json")


def write_whole(path: str, text: str) -> None:
    tmp = path + ".part"
    with open(tmp, "w") as handle:
        handle.write(text)
    os.replace(tmp, path)


def run_to(path: str, argv: list, env: dict, err_path: str) -> None:
    """Run argv with stdout to path, renamed into place only on exit 0."""
    tmp = path + ".part"
    with open(tmp, "w") as out, open(err_path, "w") as err:
        proc = subprocess.run(argv, stdout=out, stderr=err, env=env)
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(argv[:2])} exited {proc.returncode}; "
                           f"see {err_path}")
    os.replace(tmp, path)


def materialise(nodes: list, work: str, subject: str) -> list:
    os.makedirs(os.path.join(work, subject, "nodes"), exist_ok=True)
    paths = []
    for n in nodes:
        path = node_path(work, subject, n["node"])
        body = json.dumps(n["spec"], sort_keys=True) + "\n"
        if not (os.path.exists(path) and open(path).read() == body):
            write_whole(path, body)
        paths.append(path)
    return paths


def read_fingerprints(path: str) -> dict:
    """{node id: int} from fpdraw eval output, keyed by the node file's stem."""
    out = {}
    with open(path) as handle:
        for line in handle:
            p, _, digits = line.rstrip("\n").partition("\t")
            out[int(Path(p).stem)] = int(digits, 16)
    return out


def plan(subject: str, nodes: list, paths: list, fp: dict):
    """Candidate rows: every unordered pair with a direction no word refutes.

    a => b survives the words when a accepts no word b rejects, that is when
    a's accepted set is a subset of b's: fp[a] | fp[b] == fp[b]. A generator,
    since the largest subject plans millions of rows.
    """
    ids = [n["node"] for n in nodes]
    missing = [i for i in ids if i not in fp]
    if missing:
        raise RuntimeError(f"{subject}: {len(missing)} node(s) have no "
                           f"fingerprint, first {missing[0]}")
    bits = [fp[i] for i in ids]
    for x in range(len(ids)):
        fx = bits[x]
        for y in range(x + 1, len(ids)):
            fy = bits[y]
            union = fx | fy
            fwd, rev = union == fy, union == fx
            if fwd or rev:
                dirs = "both" if fwd and rev else "fwd" if fwd else "rev"
                yield (f"{subject}:{ids[x]}:{ids[y]}", paths[x], paths[y], dirs)


def unglue(subject: str) -> str:
    return "1" if subject.startswith("rad-core-") or subject == "mode-arbiter" else "0"


def do_subject(subject: str, args, env: dict) -> dict:
    t0 = time.time()
    sdir = os.path.join(args.out, subject)
    wdir = os.path.join(args.work, subject)
    os.makedirs(sdir, exist_ok=True)
    pairs_path = os.path.join(wdir, "pairs.csv")
    nodes = load_nodes(args.members, subject)
    paths = materialise(nodes, args.work, subject)
    write_whole(os.path.join(wdir, "nodes.list"), "".join(p + "\n" for p in paths))

    ltl = os.path.join(wdir, "members.ltl")
    if not os.path.exists(ltl):
        # One line per member, so draw_black.py's uniform target is uniform
        # over members, as the archive's per-run member lists made it.
        draw_list = os.path.join(wdir, "members.list")
        lines = [paths[k] + "\n" for k, n in enumerate(nodes)
                 for _ in range(len(n["members"]))]
        write_whole(draw_list, "".join(lines))
        spec = str(REPO_ROOT / "examples" / subject / "spec.json")
        run_to(ltl, [args.fpdraw, "ltl", spec, draw_list], env,
               os.path.join(sdir, "ltl.err"))
    words = os.path.join(sdir, "words")
    if not os.path.exists(words):
        tmp = words + ".part"
        with open(os.path.join(sdir, "draw.log"), "w") as err:
            proc = subprocess.run([sys.executable, args.draw_script, ltl,
                                   str(args.words), str(args.seed), tmp,
                                   str(args.workers)], stderr=err, env=env)
        if proc.returncode != 0:
            raise RuntimeError(f"{subject}: draw_black.py exited {proc.returncode}")
        # draw_black.py exits 0 whatever black said. An ERROR is a black that
        # did not run (a missing library reads the same), never a property of
        # the subject, so it fails the subject rather than thinning its words.
        with open(tmp + ".meta") as handle:
            results = [ln.split("\t")[3] for ln in handle if ln.strip()]
        errors = sum(r == "ERROR" for r in results)
        if errors or os.path.getsize(tmp) == 0:
            raise RuntimeError(f"{subject}: black answered ERROR on {errors} of "
                               f"{len(results)} draws; see draw.log")
        os.replace(tmp + ".meta", words + ".meta")
        os.replace(tmp, words)
    fp_path = os.path.join(sdir, "fp.tsv")
    if not os.path.exists(fp_path):
        run_to(fp_path, [args.fpdraw, "eval", words, os.path.join(wdir, "nodes.list"),
                         str(args.workers)],
               dict(env, FPDRAW_UNGLUE=unglue(subject)), os.path.join(sdir, "eval.err"))
    fp = read_fingerprints(fp_path)
    tmp, n_pairs = pairs_path + ".part", 0
    with open(tmp, "w", newline="") as handle:
        w = csv.writer(handle)
        w.writerow(PAIRS_HEADER)
        for row in plan(subject, nodes, paths, fp):
            w.writerow(row)
            n_pairs += 1
    os.replace(tmp, pairs_path)
    with open(words) as handle:
        n_words = sum(1 for _ in handle)
    return {"subject": subject, "nodes": len(nodes), "words": n_words,
            "pairs": n_pairs, "secs": round(time.time() - t0, 1)}


def join_pairs(subjects: list, work: str, dest: str) -> int:
    if os.path.dirname(dest):
        os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp, n = dest + ".part", 0
    with open(tmp, "w", newline="") as handle:
        w = csv.writer(handle)
        w.writerow(PAIRS_HEADER)
        for s in subjects:
            with open(os.path.join(work, s, "pairs.csv"), newline="") as src:
                reader = csv.reader(src)
                next(reader)
                for row in reader:
                    w.writerow(row)
                    n += 1
    os.replace(tmp, dest)
    return n


def file_sha256(path: str) -> str:
    try:
        with open(path, "rb") as handle:
            return hashlib.sha256(handle.read()).hexdigest()
    except OSError:
        return "?"


def version_of(binary: str) -> dict:
    try:
        proc = subprocess.run([binary, "--version"], capture_output=True,
                              text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return {}
    out = {}
    for line in proc.stdout.splitlines():
        key, sep, value = line.strip().partition("=")
        if sep:
            out[key.strip()] = value.strip()
    return out


def git_field(*argv: str) -> str:
    try:
        proc = subprocess.run(["git", "-C", str(REPO_ROOT), *argv],
                              capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return "?"
    return proc.stdout.strip() or "?"


def write_manifest(path: str, manifest: dict) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as handle:
        handle.write(json.dumps(manifest, indent=2) + "\n")
    os.replace(tmp, path)


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("subjects", help="text file, one subject a line")
    parser.add_argument("members", help="directory of <subject>.jsonl.gz")
    parser.add_argument("work", help="directory for node files and pair lists")
    parser.add_argument("out", help="output directory, one subdirectory a subject")
    parser.add_argument("--pairs", default=None,
                        help="join every subject's pair list into this CSV")
    parser.add_argument("--words", type=int, default=DEFAULTS["words"])
    parser.add_argument("--seed", type=int, default=DEFAULTS["seed"])
    parser.add_argument("--workers", type=int, default=DEFAULTS["workers"])
    parser.add_argument("--black-timeout", type=int,
                        default=DEFAULTS["black_timeout"],
                        help="black's -t per drawn word (s)")
    parser.add_argument("--fpdraw", default=os.environ.get(
        "FPDRAW", str(BUILD / "fpdraw")))
    parser.add_argument("--draw-script", default=str(DRAW_SCRIPT))
    parser.add_argument("--manifest", default=None)
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    with open(args.subjects) as handle:
        subjects = [ln.strip() for ln in handle if ln.strip() and not ln.startswith("#")]
    env = dict(os.environ, BLACK_TIMEOUT=str(args.black_timeout))
    # The checkout's black: the build's wrapper, which sets the library path
    # a source-built black needs, as draw_black.py's own default does.
    for path in (THIRD_PARTY / "black" / "black",
                 THIRD_PARTY / "black" / "install" / "bin" / "black"):
        if path.exists():
            env.setdefault("BLACK", str(path))
            break
    os.makedirs(args.out, exist_ok=True)
    progress = os.path.join(args.out, f"done-{Path(args.subjects).stem}.csv")
    done = set()
    if os.path.exists(progress):
        with open(progress, newline="") as handle:
            done = {r["subject"] for r in csv.DictReader(handle)}
    done &= {s for s in subjects
             if os.path.exists(os.path.join(args.work, s, "pairs.csv"))}

    manifest = None
    if args.manifest:
        manifest = {
            "kind": "fingerprint",
            "hostname": socket.gethostname(),
            "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "finished": None,
            "subjects": args.subjects,
            "subject_list": subjects,
            "members": args.members,
            "work": args.work,
            "out_dir": args.out,
            # status counts this file's rows against `planned`.
            "out": progress,
            "pairs": args.pairs,
            "planned": len(subjects),
            "done_at_start": len(done),
            "words": args.words, "seed": args.seed, "workers": args.workers,
            "black_timeout": args.black_timeout,
            "binaries": {
                "fpdraw": {"path": args.fpdraw, "sha256": file_sha256(args.fpdraw)},
                "peredur": {"path": str(BUILD / "peredur"),
                            **version_of(str(BUILD / "peredur"))},
                "black": env.get("BLACK", "draw_black.py default"),
            },
            "draw_script": {"path": args.draw_script,
                            "sha256": file_sha256(args.draw_script)},
            "git": {"branch": git_field("rev-parse", "--abbrev-ref", "HEAD"),
                    "head": git_field("rev-parse", "HEAD")},
        }
        write_manifest(args.manifest, manifest)

    failed = []
    for s in subjects:
        if s in done:
            continue
        log(f"{s}: start")
        try:
            row = do_subject(s, args, env)
        except Exception as exc:  # noqa: BLE001 -- one subject's failure
            failed.append(s)      # must not stop the others
            log(f"{s}: FAILED {exc}")
            continue
        new = not os.path.exists(progress)
        with open(progress, "a", newline="") as handle:
            w = csv.DictWriter(handle, list(row))
            if new:
                w.writeheader()
            w.writerow(row)
        done.add(s)
        log(f"{s}: {row['nodes']} nodes, {row['words']} words, "
            f"{row['pairs']} pairs, {row['secs']}s")

    joined = None
    if not failed and args.pairs:
        joined = join_pairs(subjects, args.work, args.pairs)
        log(f"joined {joined} pairs into {args.pairs}")
    if manifest is not None:
        manifest["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        manifest["missing"] = len(failed)
        manifest["pairs_joined"] = joined
        write_manifest(args.manifest, manifest)
    if failed:
        print(f"incomplete: {', '.join(failed)}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
