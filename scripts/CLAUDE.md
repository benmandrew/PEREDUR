# scripts

`campaign.py` drives every campaign. `README.md` beside this file documents every flag, and `docs/dev/campaigns.md` is the operating manual: what each verb does and refuses, what each `status` reading means, the queue's transitions, and how a campaign is closed. Read it before acting on a campaign.

## Rules

- Everything that acts on a campaign goes through `campaign.py`, reading `experiments/<name>/campaign.toml`. Never hand-roll an ssh launch, choose a seed range at the prompt, or edit queue TOML on a host.
- `campaign.py status` is read-only, takes about a second, and is the source of truth. Re-run it instead of recalling what it said, and never derive progress from a results CSV's length.
- `stuck` means the runner is alive and silent for three hours: investigate it. `stalled` means no runner names the profile.
- Prefer `enqueue` to waiting on a busy host; the five-minute tick starts it. Never poll in a loop and never spawn an agent to watch a run.
- Stop a campaign with `dequeue`, which kills the tick before its children. Restart a `failed` or `cancelled` entry with `requeue` after reading `experiments/queue/NNN-<name>.log`.
- `stage --force` needs the host name typed at a terminal. Never script around that.
- `--allow-stale-binary` is only for reproducing an archive at its recorded commit or topping up rows a later commit did not affect. Never use it to skip a rebuild.
- The crontab line must not wrap `tick` in `flock`: the tick then deadlocks on its own lock and logs nothing that reads as an error. `campaign.py cron --print` emits the correct line.
- `KEY_FIELDS` in `merge_experiments.py` and the resume key in `run_experiments.py` are off limits, and `commit`/`dirty` never join either.
- Launch-path changes are tested with `python3 scripts/test_campaign.py` against stubs, never on a lab host. Read its closing summary, since it reports every failure at the end.
- `campaign.py` parses TOML itself because the hosts run python 3.10 without `tomllib`. Remote shell scripts never contain a bare glob, because the hosts' zsh aborts on `NOMATCH`.
