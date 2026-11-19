# 2026-11-19-fretish-exchange-pooled

Registered 2026-11-19, before launch. On 18 Nov the user made the gate's exchange mode (`gate_realizable = "exchange"`) the default, so the paper's full is lean-remove plus exchange. `2026-11-18-fretish-exchange-rerun` reran the paper's arms under it. This is the pooled pass over that rerun, for strength, diversity and joint-front reach. It follows `2026-11-17-fretish-order-remove-no-f-pooled`, whose branch it is built on (`ff9f2119`).

## Arms and contrasts

All seven arms come from `2026-11-18-fretish-exchange-rerun`, profile `fretish-exchange-rerun`, at `e117a76` (branch `campaign/seeded-corpus-sideways`), 30 seeds each (av2 0–14, av3 15–29), so the pool reads one run tree.

| Paper name | Level |
|---|---|
| full | `remove-exchange` |
| no-remove | `lean-exchange` |
| no-climb | `remove-no-climb-exchange` |
| no-gate | `remove-no-gate` |
| no-blame | `remove-no-blame-exchange` |
| only-gate | `only-gate-exchange` |
| base | `base` |

The contrasts the paper needs are in two families, with Holm within each:

| Family | Contrasts (a against b) |
|---|---|
| `base` | full, only-gate and no-climb against base |
| `ablation` | no-climb, no-gate, no-blame and no-remove against full |

The other 14 pairs of the seven arms are in a third family, `joint`. They are there so that the fingerprint phase plans every cross-arm pair, which the joint front needs: an undecided pair reads as no implication. Their net shares are reported but are not part of the paper's tests.

## Subjects

The small set less `fsm-lmcps`, where base finds no repair, and rad's 10 cores: 18 subjects. `valu3s-uc6` and `lift-plus-cruise-mini` are sampled at 100 draws an arm. Whole `rad` and `lift-plus-cruise-full` are left out: the rerun's large profile has no base arm, has only 10 and 6 seeds, and `lift-plus-cruise-full` was still running on av2 (entry 092) when this pass was built.

## Measures

The same as `2026-11-17-fretish-order-remove-no-f-pooled`, plus joint-front reach.

1. Strength: accepted-trace share from `scripts/joint_measures.py`, over 32,768 words per subject; a lower share is a stronger arm.
2. Diversity: net containment at K = 3 from `scripts/pooled_reduce.py --pooled-balls`, with each arm's odd-against-even seed-half control.
3. Joint-front reach over all seven arms, from `scripts/joint_measures.py` without `--share-only`. The tooling has no seed-half control for reach, so reach is reported without one.

Each contrast pairs subjects with the exact Wilcoxon signed-rank p and the Hodges–Lehmann shift with its 95% interval.

## Inputs

`experiments/results-fretish-exchange-rerun` was collected on 2026-11-19 with `campaign.py collect --csv results-fretish-exchange-rerun.csv --results-dir results-fretish-exchange-rerun`: 3,990 runs (1,995 a host), every one with `run.json`. `members/` was pooled locally with `python3 scripts/pool_members.py experiments/2026-11-19-fretish-exchange-pooled/design.toml experiments/2026-11-19-fretish-exchange-pooled/members --root .`. All 3,780 runs of the 18 subjects are present, and 151 hold no member. Every arm of every subject holds at least one member, and the pools range from 99 nodes (`rad-core-10`) to 16,121 (`valu3s-uc6`). At 4.7 MB the pool exceeds the blob limit, so it is untracked.

The subject split is the template's: 15 subjects on av2, and `rad-core-33`, `valu3s-uc6` and `lift-plus-cruise-mini` on av3. The pool, `design.toml` and the subject lists are untracked and copied to each host by `rsync -a` before enqueue. Their SHA-256 sums are below.

## Hosts

av2 and av3, CPUs 0–29 and 64 GB each, the build included, approved by the user on 19 Nov. CPUs 30–31 stay free for the Ollama server. On av2 it queues behind entry 092 (the rerun's `lift-plus-cruise-full`). Compare runs 12 jobs on both hosts.

## Estimate

`2026-11-16-fretish-order-pooled-joint` planned every pair of five arms on the same subjects and was estimated at about 2 h at 16 jobs, most of it `rad-core-33`. This pool is of about its size per subject (`rad-core-33` 7,732 nodes against 11,432) and plans every pair the same way, so at 12 jobs it should take about 2–3 h per host. The estimate carries at least ±50%.

## Order of work

1. Push the branch, copy the inputs to both hosts, enqueue on both.
2. `campaign.py collect --outputs fretish-exchange-pooled`.
3. `pooled_reduce.py --pooled-balls` and `joint_measures.py` on this design, locally, one at a time under a memory cap.

## Input checksums

```
8d0a1645d04f6fed18b70a59fe064c35c73fa82d161544fce0ce404df73d87e6  design.toml
20896dc9b89dde698d76254432975e6e4ce4b7370d33d50f7cd9f6915a0bb785  subjects-av2.txt
855f78e4c637cbe7837c5da9cc7c40be51c4afd02754f6d6d7f9ea8567b4abf8  subjects-av3.txt
8591a4672153baf31bac3efaf53f0c3031ccf157a54d94f5f00c2300bf10f492  members/done.csv
61db37e720199fc38002929149c0bc6f4cc7275c043b8a229d439fb92c650718  members/fsm-combined.jsonl.gz
39719e2b1f47a1f7982fc83f5984970f7739e82a53aecd76feabe1d81f185b7e  members/fsm.jsonl.gz
226dad1e9c319bd91b8ed091fd42b5a0b245b655bf03bf9dea7fd471349917fd  members/fsm-timing.jsonl.gz
2b1587f5086fb68c9a561f736f9a4e07003afaddcb2dcb169a41792720088275  members/index.csv
90a30fae53f6c7071a386e54f7430aa962fc3ac8fd5fba37ae504c5852e74c8c  members/lift-plus-cruise-mini.jsonl.gz
113a2482dfcd69c276067ad505aa6922d5f598717fe618389737a950e1763cd1  members/liquid-mixer.jsonl.gz
5e3d560f8291b9d081100eb83de52a91b6f3060b8cb848656bf280d922e29f4e  members/mode-arbiter.jsonl.gz
aa94785f116957d451361bbc062f49c1b535b08efbd97fc5130f636fdf2b89bb  members/rad-core-10.jsonl.gz
e680c440193f720b6983d3d0f78caab17c6ea406d5b0a3ce4c58cf3f301a4ce8  members/rad-core-1-18.jsonl.gz
08a221b0989b491722f1aa65dab93bde813a342cad1e787cac1a0d24e60745e8  members/rad-core-12-18.jsonl.gz
ab3ac953e2d08c651fc389cb84b4faf39073237cc856fee26e5f96670a011951  members/rad-core-17-18.jsonl.gz
bf9ada0c85ab2f6114b7053b0eb9ab3e03cafe7be1d37d09ea6cd05a185f9146  members/rad-core-33.jsonl.gz
40e6e191c22811e1c9e9729f7510cb00a07b27a8f80c52effaf963ad0f38a57c  members/rad-core-45.jsonl.gz
86dbb28c2edb5d41818a2c442b0d3fa74f353fe19723c1551374174dcfb1a137  members/rad-core-55.jsonl.gz
d08bc492d401ab4b1023c2998f3821407d235617dba2bc1368eb2f3d98217487  members/rad-core-61.jsonl.gz
103212241a09da1316a62af64a5d01b4ec3f0a5df740bde988be120d87f1bfad  members/rad-core-65.jsonl.gz
96828d5bac642d72936787dbbdd29fd2ad4846576119de3e2703d408989ad872  members/rad-core-68.jsonl.gz
b216e278a7193777b887c61ae30a0c0ffca225fd7683e7b46fb5e8ab4c442a54  members/takeoff.jsonl.gz
1b92aebb376b15b4a3c30cf50e2548792f037b863eac1fe883dd6f6bdb48b394  members/valu3s-uc6.jsonl.gz
```
