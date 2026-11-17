#!/usr/bin/env python3
"""Run the full4-exchange work list on one host, outside the campaign queue.

One lane per CPU. Each lane has a start time, a time after which it takes no
new item, and the item kinds it may take. Items are taken in list order under
one lock, so a lane that may take both kinds takes the required items before
the optional ones. Each item is one run_experiments.py invocation for one
profile, spec and seed, pinned to the lane's CPU with `taskset -c` at one job.
A failed item is run once more (the runner resumes); a second failure skips it.
At the hard stop every running item is killed and the driver exits.

Every event is one line of the status file, `<UTC ISO time> <event> k=v ...`,
so that one `ssh host cat <file>` shows the whole run.

    driver.py --host av2 [--schedule s.json] [--no-build]
    driver.py --host test --schedule s.json --runner 'cmd {profile} {spec} {seed}'
"""

import argparse
import csv
import datetime as dt
import json
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CAMPAIGN = HERE.parent
ROOT = CAMPAIGN.parent.parent

REQUIRED, OPTIONAL = "required", "optional"


def utc_now() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def parse_time(text: str) -> dt.datetime:
    """An ISO time; one without a zone is read as UTC."""
    t = dt.datetime.fromisoformat(text.replace("Z", "+00:00"))
    return t if t.tzinfo else t.replace(tzinfo=dt.timezone.utc)


class Status:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        self.lock = threading.Lock()

    def event(self, name: str, **fields) -> None:
        stamp = utc_now().strftime("%Y-%m-%dT%H:%M:%SZ")
        tail = " ".join(f"{k}={v}" for k, v in fields.items())
        line = f"{stamp} {name} {tail}".rstrip() + "\n"
        with self.lock, open(self.path, "a") as f:
            f.write(line)
            f.flush()
        print(line, end="", flush=True)


def work_list(host: dict) -> list[dict]:
    """The host's items in priority order: each block's seeds, seed-major."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import run_experiments as rx  # noqa: E402
    items = []
    for block in host["blocks"]:
        profile = rx.PROFILES[block["profile"]]
        specs = profile["specs"]
        if not specs:
            sys.exit(f"profile {block['profile']} has no specs on this host")
        for seed in block["seeds"]:
            for spec in specs:
                items.append({"profile": block["profile"], "spec": spec,
                              "seed": seed, "kind": block["kind"]})
    return items


def test_work_list(host: dict) -> list[dict]:
    items = []
    for block in host["blocks"]:
        for seed in block["seeds"]:
            for spec in block["specs"]:
                items.append({"profile": block["profile"], "spec": spec,
                              "seed": seed, "kind": block["kind"]})
    return items


def row_done(item: dict) -> bool:
    """True when the profile's CSV holds the item's row."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import run_experiments as rx  # noqa: E402
    profile = rx.PROFILES[item["profile"]]
    level = next(iter(profile["levels"].values()))[0]
    path = profile["results_csv"]
    if not path.is_file():
        return False
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            if (row.get("level_name") == level and row.get("spec") == item["spec"]
                    and row.get("seed") == str(item["seed"])):
                return True
    return False


def write_headers(items: list[dict]) -> None:
    """Write each CSV's header before any lane starts, so that two lanes that
    finish their first run together cannot both write one."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import run_experiments as rx  # noqa: E402
    for name in sorted({i["profile"] for i in items}):
        path = rx.PROFILES[name]["results_csv"]
        if not path.exists() or path.stat().st_size == 0:
            path.parent.mkdir(parents=True, exist_ok=True)
            with open(path, "w", newline="") as f:
                csv.writer(f).writerow(rx.CSV_FIELDS)


class Driver:
    def __init__(self, items, lanes, hard_stop, status, runner, log_dir):
        self.items = items
        self.taken = [False] * len(items)
        self.lanes = lanes
        self.hard_stop = hard_stop
        self.status = status
        self.runner = runner
        self.log_dir = log_dir
        self.lock = threading.Lock()
        self.procs: dict[int, subprocess.Popen] = {}
        self.stopping = threading.Event()

    def take(self, kinds) -> int | None:
        with self.lock:
            for i, item in enumerate(self.items):
                if not self.taken[i] and item["kind"] in kinds:
                    self.taken[i] = True
                    return i
        return None

    def command(self, item, cpu) -> list[str]:
        if self.runner:
            cmd = self.runner.format(**item, cpu=cpu)
            return ["taskset", "-c", str(cpu), "sh", "-c", cmd]
        return ["taskset", "-c", str(cpu), sys.executable,
                str(ROOT / "scripts" / "run_experiments.py"),
                "--profile", item["profile"], "--specs", item["spec"],
                "--seeds", str(item["seed"]), "--jobs", "1"]

    def run_once(self, item, cpu, attempt, lane_id) -> bool:
        tag = f"{item['profile']}__{item['spec']}__seed{item['seed']}"
        log = self.log_dir / f"{tag}.attempt{attempt}.log"
        with open(log, "w") as out:
            proc = subprocess.Popen(self.command(item, cpu), cwd=ROOT,
                                    stdout=out, stderr=subprocess.STDOUT,
                                    start_new_session=True)
            with self.lock:
                self.procs[lane_id] = proc
            rc = proc.wait()
            with self.lock:
                self.procs.pop(lane_id, None)
        if self.stopping.is_set():
            return False
        if self.runner:
            return rc == 0
        return rc == 0 and row_done(item)

    def lane(self, lane_id, cpu, start, stop_new, kinds) -> None:
        while utc_now() < start and not self.stopping.is_set():
            time.sleep(min(5.0, max(0.05, (start - utc_now()).total_seconds())))
        if self.stopping.is_set():
            return
        self.status.event("lane-start", cpu=cpu)
        while not self.stopping.is_set():
            if utc_now() >= stop_new:
                self.status.event("lane-stopped", cpu=cpu, reason="cutoff")
                return
            i = self.take(kinds)
            if i is None:
                self.status.event("lane-stopped", cpu=cpu, reason="no-items")
                return
            item = self.items[i]
            fields = dict(profile=item["profile"], spec=item["spec"],
                          seed=item["seed"], kind=item["kind"], cpu=cpu)
            for attempt in (1, 2):
                if attempt == 2 and utc_now() >= stop_new:
                    # A retry is a new run, which the cutoff forbids.
                    break
                self.status.event("item-start", attempt=attempt, **fields)
                t0 = time.monotonic()
                ok = self.run_once(item, cpu, attempt, lane_id)
                secs = round(time.monotonic() - t0)
                if self.stopping.is_set():
                    self.status.event("item-killed", secs=secs, **fields)
                    return
                if ok:
                    self.status.event("item-done", attempt=attempt, secs=secs,
                                      **fields)
                    break
                self.status.event("item-failed", attempt=attempt, secs=secs,
                                  **fields)
            else:
                self.status.event("item-skipped", **fields)

    def kill_all(self) -> None:
        self.stopping.set()
        with self.lock:
            procs = list(self.procs.values())
        for proc in procs:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        time.sleep(5)
        for proc in procs:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def run(self) -> None:
        threads = []
        for lane_id, lane in enumerate(self.lanes):
            t = threading.Thread(target=self.lane, daemon=True, args=(
                lane_id, lane["cpu"], parse_time(lane["start"]),
                parse_time(lane["stop_new"]), set(lane["kinds"])))
            t.start()
            threads.append(t)
        while any(t.is_alive() for t in threads):
            if utc_now() >= self.hard_stop:
                running = len(self.procs)
                self.kill_all()
                self.status.event("hard-stopped", running=running)
                for t in threads:
                    t.join(timeout=10)
                return
            time.sleep(1)
        left = sum(1 for t in self.taken if not t)
        self.status.event("finished", untaken=left)


def build(status: Status, cpus: str) -> bool:
    status.event("build-start", cpus=cpus)
    steps = [["cmake", "--preset", "release"],
             ["cmake", "--build", "build-release", "-j", "4"]]
    log = CAMPAIGN / "build.log"
    with open(log, "a") as out:
        for step in steps:
            rc = subprocess.call(["taskset", "-c", cpus, *step], cwd=ROOT,
                                 stdout=out, stderr=subprocess.STDOUT)
            if rc != 0:
                status.event("build-failed", step=" ".join(step), rc=rc)
                return False
    status.event("build-done")
    return True


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True)
    ap.add_argument("--schedule", default=None,
                    help="a JSON schedule in place of schedule.py's (tests)")
    ap.add_argument("--run-dir", default=None,
                    help="where the status file and item logs go "
                         "(default: run-<host> beside this script)")
    ap.add_argument("--runner", default=None,
                    help="test only: a shell command in place of "
                         "run_experiments.py, with {profile} {spec} {seed} {cpu}")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the schedule and work list, then exit: no "
                         "build, no configs, no runs, no status file")
    args = ap.parse_args()

    if args.schedule:
        host = json.loads(Path(args.schedule).read_text())[args.host]
    else:
        sys.path.insert(0, str(HERE))
        from schedule import SCHEDULE  # noqa: E402
        host = SCHEDULE[args.host]
    if args.dry_run:
        items = test_work_list(host) if args.runner else work_list(host)
        print(f"host {args.host}: hard stop {host['hard_stop']}, "
              f"build on CPUs {host['build_cpus']}")
        for lane in host["lanes"]:
            print(f"  lane cpu {lane['cpu']}: {lane['start']} to "
                  f"{lane['stop_new']}, kinds {','.join(lane['kinds'])}")
        for block in host["blocks"]:
            n = sum(1 for i in items if i["profile"] == block["profile"])
            print(f"  {block['kind']:8} {block['profile']}: seeds "
                  f"{block['seeds'][0]}-{block['seeds'][-1]}, {n} items")
        print(f"  first item: {items[0]}" if items else "  no items")
        return
    run_dir = (Path(args.run_dir) if args.run_dir
               else CAMPAIGN / f"run-{args.host}")
    (run_dir / "logs").mkdir(parents=True, exist_ok=True)
    status = Status(run_dir / "status.log")
    status.event("start", host=args.host, pid=os.getpid())
    hard_stop = parse_time(host["hard_stop"])

    if not args.no_build and not args.runner:
        if not build(status, host["build_cpus"]):
            sys.exit(1)
    if host.get("configs") and not args.runner:
        rc = subprocess.call(host["configs"], shell=True, cwd=ROOT,
                             stdout=subprocess.DEVNULL)
        if rc != 0:
            status.event("configs-failed", rc=rc)
            sys.exit(1)
        status.event("configs-done")

    items = test_work_list(host) if args.runner else work_list(host)
    if not args.runner:
        write_headers(items)
    status.event("work-list", items=len(items),
                 required=sum(i["kind"] == REQUIRED for i in items),
                 optional=sum(i["kind"] == OPTIONAL for i in items))
    Driver(items, host["lanes"], hard_stop, status, args.runner,
           run_dir / "logs").run()


if __name__ == "__main__":
    main()
