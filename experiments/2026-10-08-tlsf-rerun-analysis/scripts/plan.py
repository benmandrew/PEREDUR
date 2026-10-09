#!/usr/bin/env python3
"""Local planning for 2026-10-08-tlsf-rerun-analysis, after collect.

    python3 experiments/2026-10-08-tlsf-rerun-analysis/scripts/plan.py [--specs a,b]

Run from the checkout root once these are local (campaign.py collect):
experiments/results-tlsf-rerun/ (bodies), experiments/curves-tlsf-rerun/<host>/
(members.tsv) and experiments/separation-recount-tlsf-rerun-s0/<host>/
(fingerprints.tsv). Writes the work tree the host phases read,
experiments/analysis-tlsf-rerun-work/, and packs it into the committed
bundles under the campaign's data/ (unpack.py restores them on a host):

sidecars/<host>/   links pairing each run's members.tsv with its
                   fingerprints.tsv, which every reader expects side by side.
strength/          pooled_frontier.py plan over the selection and grading
                   comparisons, all 25 families, dealt to av1, av2 and av3.
rq3/               PEREDUR (mrs-nsga2-apportion) pool against AuRUS's archived
                   pooled frontier (data/aurus-frontier.csv, aurus_frontier.py),
                   every class pair that the 65,536-word fingerprints do not
                   refute in both directions, in compare_pairs.py's format.
                   Each pair goes to the host holding its AuRUS body.
coverage/          the GR(1) coverage pool (maoz_score.py's pool pass over
                   the rerun) and every (pool repair, tool frontier
                   representative) pair of the archived Maoz frontier.

Paths in pair lists are relative to the host checkout.
"""
import argparse
import csv
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
CAMPAIGN = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(CAMPAIGN))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import pooled_frontier as pf  # noqa: E402

EXP = "experiments"
WORK = f"{EXP}/analysis-tlsf-rerun-work"
RESULTS = f"{EXP}/results-tlsf-rerun"
CURVES = f"{EXP}/curves-tlsf-rerun"
RECOUNT = f"{EXP}/separation-recount-tlsf-rerun-s0"
HOSTS = ("av1", "av2", "av3")
SPECS = ("arbiter-aurus detector-aurus full-arbiter-aurus gyro-var1 gyro-var2 humanoid-458 "
         "humanoid-503 humanoid-531 humanoid-742 lift lily02 lily11 lily15 lily16 "
         "load-balancer-aurus ltl2dba-r-2 ltl2dba-theta-2 ltl2dba27 minepump pcar-v2-888 "
         "prioritized-arbiter-aurus rg1 rg2 round-robin-arbiter-aurus simple-arbiter-aurus").split()
# The archived RQ3's 20 families: humanoid-742 was never run there, and four
# families have no screened AuRUS run.
RQ3_SPECS = [s for s in SPECS if s not in ("humanoid-742", "humanoid-503", "humanoid-531",
                                           "full-arbiter-aurus", "prioritized-arbiter-aurus")]
ARMS = {"mrs-nsga2-apportion": "sweep_G_mrs_nsga2-apportion_wkoff_log",
        "mrs-weighted": "sweep_G_mrs_weighted_wkoff_log",
        "aurus-nsga2-apportion": "sweep_G_aurus_nsga2-apportion_wkoff_log"}
COMPARISONS = {"selection": ["mrs-nsga2-apportion", "mrs-weighted"],
               "grading": ["mrs-nsga2-apportion", "aurus-nsga2-apportion"]}
# The archived AuRUS sidecars (well-separation screened), whose prints share
# the rerun recount's word draw: 65,536 words, seed 0, 8/8 lassos.
AURUS_SIDECARS = os.environ.get(
    "AURUS_SIDECARS", os.path.expanduser(
        "~/projects/counter-wt/repair-diversity/experiments/2026-10-01-fretish-mixed/"
        "diversity/ball-radii/out/ws/aurus"))
MAOZ = os.environ.get("MAOZ_ROOT", os.path.expanduser("~/projects/counter-wt/maoz"))
MAOZ_SUBJECTS = ("rg1", "rg2", "lift", "gyro-var1", "gyro-var2", "humanoid-458",
                 "humanoid-503", "humanoid-531", "humanoid-742", "pcar-v2-888")


def link_sidecars() -> None:
    """sidecars/<host>/<run>.{members,fingerprints}.tsv as relative links."""
    for host in HOSTS:
        d = f"{WORK}/sidecars/{host}"
        os.makedirs(d, exist_ok=True)
        for m in glob.glob(f"{CURVES}/{host}/*.members.tsv"):
            run = os.path.basename(m)[:-len(".members.tsv")]
            fp = f"{RECOUNT}/{host}/{run}.fingerprints.tsv"
            if not os.path.exists(fp):
                continue  # a run with no recount has no prints; members.py skips it too
            for src, name in ((m, f"{run}.members.tsv"), (fp, f"{run}.fingerprints.tsv")):
                dst = f"{d}/{name}"
                if not os.path.lexists(dst):
                    os.symlink(os.path.relpath(src, d), dst)


def plan_strength(specs: list) -> None:
    sides = {"seeds": "0-29", "comparisons": COMPARISONS, "sides": {
        label: {"members": [f"{WORK}/sidecars/{h}/{prefix}_{{spec}}_seed{{seed:02d}}.members.tsv"
                            for h in HOSTS],
                "body": f"{RESULTS}/{prefix}_{{spec}}_seed{{seed:02d}}/accumulated/{{file}}"}
        for label, prefix in ARMS.items()}}
    os.makedirs(f"{WORK}/strength", exist_ok=True)
    path = f"{WORK}/strength-sides.json"
    with open(path, "w") as fh:
        json.dump(sides, fh, indent=1)
    if pf.main(["plan", path, f"{WORK}/strength", "--specs", ",".join(specs),
                "--hosts", ",".join(HOSTS)]) != 0:
        raise SystemExit("pooled_frontier plan failed")


def aurus_prints(spec: str, rows: list) -> dict:
    out = {}
    by_run = {}
    for r in rows:
        by_run.setdefault((r["host"], int(r["seed"])), []).append(r)
    for (host, seed), rs in by_run.items():
        p = f"{AURUS_SIDECARS}/{host}/aurus_{spec}_seed{seed:02d}.fingerprints.tsv"
        got = pf.load_prints(p, [r["file"] for r in rs])
        for r in rs:
            if r["file"] not in got:
                raise SystemExit(f"{spec}: no AuRUS print for {host} seed {seed} {r['file']} in {p}")
            out[r["md5"]] = int(got[r["file"]], 16)
    return out


def plan_rq3(specs: list) -> dict:
    with open(os.path.join(CAMPAIGN, "data", "aurus-frontier.csv"), newline="") as fh:
        front = list(csv.DictReader(fh))
    os.makedirs(f"{WORK}/rq3", exist_ok=True)
    writers, handles, counts = {}, {}, {}
    for host in ("av2", "av3"):
        handles[host] = open(f"{WORK}/rq3/pairs-{host}.csv", "w", newline="")
        writers[host] = csv.writer(handles[host])
        writers[host].writerow(["id", "a_path", "b_path"])
    summary = {}
    for spec in specs:
        fam = pf.Family(f"{WORK}/strength", spec)
        pool = fam.pool("mrs-nsga2-apportion")
        prints = {}
        with open(f"{WORK}/strength/{spec}/prints.tsv") as fh:
            for line in fh:
                md5, _, hexed = line.rstrip("\n").partition("\t")
                prints[md5] = int(hexed, 16)
        rows = [r for r in front if r["spec"] == spec]
        aprint = aurus_prints(spec, rows)
        planned = refuted = 0
        for a in pool:
            pa = prints[a]
            for r in rows:
                pb = aprint[r["md5"]]
                if pa & ~pb and pb & ~pa:
                    refuted += 1
                    continue
                writers[r["host"]].writerow([f"rq3|{spec}|{a}|{r['md5']}",
                                             f"{WORK}/strength/{spec}/classes/{a}.tlsf", r["path"]])
                counts[r["host"]] = counts.get(r["host"], 0) + 1
                planned += 1
        summary[spec] = {"pool": len(pool), "aurus_frontier": len(rows),
                         "pairs": planned, "refuted": refuted}
        print(f"rq3 {spec}: pool {len(pool)} x AuRUS frontier {len(rows)}, "
              f"{planned} pairs, {refuted} refuted by a word both ways", flush=True)
    for h in handles.values():
        h.close()
    with open(f"{WORK}/rq3/plan.json", "w") as fh:
        json.dump({"specs": summary, "pairs_by_host": counts}, fh, indent=1)
    return counts


def maoz_md5(text: str) -> str:
    """maoz_score.py's pool key: `//` comments and all whitespace removed."""
    return hashlib.md5(re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", text)).encode()).hexdigest()


def plan_coverage() -> dict:
    """maoz_score.py --pass pool over the rerun, then the coverage pairs."""
    out = f"{WORK}/coverage"
    pool_rows, run_rows, written = [], [], set()
    arm = ARMS["mrs-nsga2-apportion"]
    for spec in MAOZ_SUBJECTS:
        for seed in range(30):
            run = f"{arm}_{spec}_seed{seed:02d}"
            found = [h for h in HOSTS if os.path.exists(f"{CURVES}/{h}/{run}.members.tsv")]
            if len(found) != 1:
                raise SystemExit(f"{run}: members on {found or 'no host'}")
            rows = []
            with open(f"{CURVES}/{found[0]}/{run}.members.tsv", newline="") as fh:
                rows = [(float(r["cut_s"]), r["file"]) for r in csv.DictReader(fh, delimiter="\t")]
            last = max(c for c, _ in rows) if rows else None
            files = sorted({f for c, f in rows if c == last})
            run_rows.append([spec, run, seed, found[0], "" if last is None else last, len(files)])
            for name in files:
                with open(f"{RESULTS}/{run}/accumulated/{name}") as fh:
                    text = fh.read()
                md5 = maoz_md5(text)
                if (spec, md5) not in written:
                    os.makedirs(f"{out}/pool/{spec}", exist_ok=True)
                    with open(f"{out}/pool/{spec}/{md5}.tlsf", "w") as fh:
                        fh.write(text)
                    written.add((spec, md5))
                pool_rows.append([spec, run, seed, name, md5])
    with open(f"{out}/pool/pool.tsv", "w", newline="") as fh:
        w = csv.writer(fh, delimiter="\t")
        w.writerow(["spec", "run", "seed", "file", "md5"])
        w.writerows(pool_rows)
    with open(f"{out}/pool/runs.tsv", "w", newline="") as fh:
        w = csv.writer(fh, delimiter="\t")
        w.writerow(["spec", "run", "seed", "host", "last_cut_s", "n_frontier"])
        w.writerows(run_rows)
    # The archived tool frontiers (2026-10-05-maoz-coverage, provenance/maoz-coverage).
    frontier_dir = f"{MAOZ}/experiments/maoz-coverage-frontier"
    with open(f"{frontier_dir}/frontier.csv", newline="") as fh:
        reps = [r for r in csv.DictReader(fh) if r["representative"] == "1"]
    pairs = []
    for r in reps:
        src = f"{MAOZ}/experiments/results-maoz-baselines/maoz-{r['tool']}_{r['spec']}_seed00/accumulated/{r['file']}"
        dst = f"{out}/tools/{r['tool']}/{r['spec']}/{r['file']}"
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        for md5 in sorted(m for s, m in written if s == r["spec"]):
            pairs.append([f"cov|{r['tool']}|{r['spec']}|{r['file']}|{md5}",
                          f"{out}/pool/{r['spec']}/{md5}.tlsf", dst])
    # Dealt round-robin: the pairs are uniform enough that counts balance load.
    counts = {}
    for i, host in enumerate(HOSTS):
        mine = pairs[i::len(HOSTS)]
        counts[host] = len(mine)
        with open(f"{out}/pairs-{host}.csv", "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["id", "a_path", "b_path"])
            w.writerows(mine)
    print(f"coverage: {len(pool_rows)} frontier rows, {len(written)} distinct, "
          f"{len(reps)} tool representatives, {len(pairs)} pairs", flush=True)
    return counts


# The bundle's prints.tsv keeps the first BUNDLE_WORDS words of each print
# (the last BUNDLE_WORDS/4 hex digits: word 0 is the last digit). The host
# reads it only to drop fallback pairs a word refutes both ways, which stays
# sound on fewer words and only lets more pairs through to compare; the walk
# itself draws its own prefilter words. The full 65,536-word prints would
# make a 1.5 GB bundle.
BUNDLE_WORDS = 4096


def truncated_prints(path: str) -> bytes:
    keep = BUNDLE_WORDS // 4
    out = []
    with open(path) as fh:
        for line in fh:
            md5, _, hexed = line.rstrip("\n").partition("\t")
            out.append(f"{md5}\t{hexed[-keep:]}\n")
    return "".join(out).encode()


def pack() -> None:
    """One bundle per subtree, under data/, with sha256s in data/bundles.json."""
    import io
    data = os.path.join(CAMPAIGN, "data")
    manifest = {}

    def skip(ti):
        name = ti.name
        if name.endswith(("result.json", ".results.csv", "prints.tsv")) or "/cache" in name:
            return None
        return ti

    for name, members in (("strength", ["strength", "strength-sides.json"]),
                          ("rq3", ["rq3"]), ("coverage", ["coverage"])):
        path = os.path.join(data, f"{name}.tar.xz")
        with tarfile.open(path, "w:xz", preset=6) as tar:
            for m in members:
                tar.add(f"{WORK}/{m}", arcname=m, filter=skip)
            if name == "strength":
                for p in sorted(glob.glob(f"{WORK}/strength/*/prints.tsv")):
                    blob = truncated_prints(p)
                    ti = tarfile.TarInfo(os.path.relpath(p, WORK))
                    ti.size, ti.mtime, ti.mode = len(blob), int(os.path.getmtime(p)), 0o644
                    tar.addfile(ti, io.BytesIO(blob))
        h = hashlib.sha256()
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        manifest[f"{name}.tar.xz"] = h.hexdigest()
        print(f"packed {path}: {os.path.getsize(path) / 1e6:.1f} MB", flush=True)
    with open(os.path.join(data, "bundles.json"), "w") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--specs", default=",".join(SPECS))
    ap.add_argument("--skip", default="", help="comma-separated steps to skip: sidecars,strength,rq3,coverage,pack")
    args = ap.parse_args()
    os.chdir(ROOT)
    specs = args.specs.split(",")
    skip = set(filter(None, args.skip.split(",")))
    if "sidecars" not in skip:
        link_sidecars()
    if "strength" not in skip:
        plan_strength(specs)
    counts = {}
    if "rq3" not in skip:
        counts["rq3"] = plan_rq3([s for s in RQ3_SPECS if s in specs])
    if "coverage" not in skip:
        counts["coverage"] = plan_coverage()
    if counts:
        with open(f"{WORK}/plan-summary.json", "w") as fh:
            json.dump(counts, fh, indent=1)
    if "pack" not in skip:
        pack()
    print(json.dumps(counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
