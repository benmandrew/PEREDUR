# 2026-10-02-maoz-baselines

Pre-registered 2026-10-05, before any row of this campaign existed. Only the smoke tests in section 8 had run.

## 1. Why this campaign exists

The paper compares PEREDUR with one other repair tool, AuRUS. Brizzio et al. (2023) compare AuRUS with three more: GLASS, JVTS-Repair and AMT13, all from the artifact of Maoz, Ringert and Shalom (ICSE 2019). Their Table 3 reports repair overlap on 11 *GR(1)* (generalised reactivity of rank 1) cases, and 10 of those are PEREDUR subjects: `rg1`, `rg2`, `lift`, `gyro-var1`, `gyro-var2`, `humanoid-458`, `humanoid-503`, `humanoid-531`, `humanoid-742` and `pcar-v2-888`.

The three tools repair only by adding environment assumptions, and they read only Spectra. The paper claims that GR(1) assumption repair misses repairs that weaken guarantees. This campaign gives that claim data on the shared subjects, and it adds three columns beside AuRUS in RQ3.

## 2. What runs

The tools are the Java classes of `SymbolicRepairsArtifact.zip` (sha256 `2643d353a86cbdcffb1c6fdc0fc99700670cff406d416b2f6e1b3b2365d0b6ce`, from smlab.cs.tau.ac.il/syntech/repair/). The artifact names them by code: `UF` is GLASS, `BFS` is JVTS-Repair and `ALUR` is AMT13.

The artifact's `RepairExporterExec` prints repairs only when a search ends, so a run stopped at a cap prints nothing. `driver/StreamRepairs.java` builds each algorithm with the exporter's own arguments and prints each repair when the algorithm records it, dated in milliseconds from the start of the search. The arguments are `TheUltimateFixer(gi, true, true)`, `BFSModelRepair(gi, -1, false, false, false, true)` and `SpecificationRefinement(gi, {P1..P4 empty}, -1, 3, true)`, with depth -1 (no bound). On `lift` the driver gave the exporter's counts and text: GLASS 1 repair, JVTS-Repair 805 and AMT13 2.

The driver prints a repair only the first time its assumption set appears. A repair's set is its `asm` statements with whitespace collapsed, in any order. JVTS-Repair records one set many times: on `lift` its 805 repairs are 221 distinct sets, and 7 of those differ from another only in the order of their assumptions. A repeat adds nothing to any endpoint in section 5, and without this rule it would also count towards the cap below. A printed repair's index counts distinct repairs, and its time is the first time its set was seen. `@@DONE` and `@@CAP` also report the raw count of recorded repairs, repeats included. `scripts/maoz_adapt.py` applies the same rule again as a guard.

The artifact expects Windows and a CUDD library. Without them it falls back to its pure-Java BDD package, `JTLVJavaFactory`, which runs on Linux under OpenJDK 21. Every run here uses that package.

av2 holds the staged directory `~/projects/tools/maoz-icse2019`: the exporter jar and its lib directory unchanged, the driver compiled there with `javac --release 8`, and `DIGEST.txt`. The digest `2212f3c7c209` is the first 12 hex digits of the SHA-256 of the jar's bytes followed by the driver source's bytes. `stage` and `scripts/maoz_campaign.py` both refuse a directory whose digest differs from `campaign.toml`.

## 3. Inputs

`inputs/` holds one Spectra file per subject, taken from the artifact's `EfficacyTest/` with carriage returns removed, and `patterns/DwyerPatterns.spectra` beside them for the `import` in `rg1` and `rg2`. Seven are unchanged. Three are edited so that each states the specification of `examples/<spec>/spec.tlsf`:

- **rg1** (`Simple/RG1.spectra`). The atoms take the TLSF names, and `asm GF !r;` is added, which the TLSF file has and the artifact's file lacks.
- **rg2** (`Simple/RG2.spectra`). The atoms take the TLSF names.
- **humanoid-531** (`SYNTECH15/HumanoidLTL_531_Humanoid_unrealizable.spectra`). Spectra reads `Y(...)` as false at the first instant, so the artifact's `G ((moveMode = FWD & Y(moveMode = TURN_LEFT_2)) <-> ...)` forbids starting with both motors at `CALIB_FWD`. The TLSF file has no such constraint, and the input uses its form: the same law from the second instant on.

`scripts/maoz_inputs_check.py` translates each input with `scripts/spectra_ltl.py` and checks it against the TLSF file with `ltlfilt --equivalent-to`, side by side: environment initial condition, the rest of the environment, and the system. `inputs-check.txt` records all ten as `MATCH`, re-checked on av2 on 2026-10-05.

## 4. Design

Three algorithms over ten subjects is 30 jobs. All three algorithms are deterministic, so each job runs once, and seed 0 is the only seed. A job ends at 1000 distinct repairs or at 7200 s, whichever comes first. The 7200 s cap is the one PEREDUR and AuRUS have, enforced by killing the process group. The repair cap matches AuRUS's stop at 1000 individuals. The driver enforces it by exiting after the 1000th distinct repair. The search is deterministic, so a capped job keeps the same 1000 repairs on every run. Every repair printed before either stop is kept, and `result.json` records the distinct count, the raw count and whether the job was capped.

The campaign runs as a `kind = "maoz"` phase on av2 alone, at concurrency 10 with a 10 GB heap per *Java virtual machine* (JVM).

`scripts/maoz_adapt.py` runs at the end of the phase. It translates each repair's assumptions into the subject's TLSF encoding and adds them to a copy of `examples/<spec>/spec.tlsf`. An assumption with no temporal operator goes to `INITIALLY`, `G φ` with no past operator goes to `REQUIRE` as `φ`, and everything else goes to `ASSUMPTIONS`. The copies go to `experiments/results-maoz-baselines/maoz-<tool>_<spec>_seed00/accumulated/spec<i>.tlsf`, with the `index.tsv` and `run.json` that `aurus_adapt.py` also writes. A repair with any assumption the translator cannot read is left out whole and counted in `maoz-adapt.json` and `maoz-translation.csv`.

## 5. Primary endpoint

The primary endpoint is per (tool, subject). It has two parts.

1. **Admissible repairs.** The count of the tool's repairs that are translated, realisable under `realize` (ltlsynt), and well-separated under `scripts/check_well_separated.py`. These are the screens AuRUS's repairs already pass.
2. **Overlap with PEREDUR.** Each admissible repair on the tool's maximal frontier is classed against PEREDUR's maximal frontier from `2026-09-14-paper-rerun`. The classes are the implication-based ones of the 2026-09-25 overlap pass, extended from two tools to five.

The secondary endpoint is the ε-separation count of each tool's maximal set at ε 0.05, 0.2 and 0.5. It is computed with the budgets of `2026-10-01-separation-recount`.

No significance test is run. The sample is one deterministic run on each of ten subjects, so each number is reported per subject.

## 6. Decision rule

- **Outcome 1: a tool has an admissible repair on a subject that PEREDUR's frontier does not cover.** The subject and the repair are reported, and RQ3's text names them.
- **Outcome 2: PEREDUR's frontier covers every admissible repair of every tool.** It is reported as coverage on the GR(1) subset, with the counts.
- **Outcome 3: a tool has no admissible repair on a subject.** It is reported as zero, and the reason is given: no repair found, killed with none printed, untranslatable, unrealisable, or not well-separated.

In every outcome the paper restricts these columns to the ten GR(1) subjects, and the comparison does not enter RQ1 or RQ2. A tool that answers in milliseconds and draws no seed gives no found-repair contrast to test.

## 7. Registered hazards

- **Spectra and ltlsynt disagree on realisability.** Spectra sees an enum's declared values. ltlsynt sees the bits, and a 7-valued enum in 3 bits leaves one code unused. A repair Spectra calls realisable can fail `realize`, and the screen in section 5 decides.
- **The BDD package is slower than CUDD.** Brizzio et al. ran on CUDD with a 10-minute cap and report JVTS-Repair timeouts on 503 and 531, and AMT13 timeouts on 503, 531, 742 and pcar. Timeouts here run on a different engine at a different cap, so they are not compared with Table 3.
- **The repair cap truncates a search.** A capped job reports the first 1000 distinct repairs in the algorithm's own order, which is not a sample of all its repairs. Each capped job is reported as capped, with its raw count. Without the cap, JVTS-Repair's output had no bound: 805 recorded repairs on `lift` in under 4 s, each one a TLSF file to screen.
- **A repair can name a system variable.** GLASS's repairs on `humanoid-531`, `humanoid-742` and `pcar-v2-888` constrain `state`, `turnState`, `specState` and `spec_policy`. Each is declared in both the Spectra file and the TLSF file, so each translates, and the well-separation screen judges it.
- **A kill can cut a repair mid-write.** The adapter drops a block with no `@@END`. A repair's date is the driver's clock from the start of the search, which excludes JVM start-up and parsing.
- **A JVM can run out of heap.** It then exits non-zero before the cap. `result.json` records the exit code, and the repairs printed before the exit are kept.

## 8. Smoke test, 2026-10-05

GLASS ran over all ten inputs on av2 with a 120 s cap, from a copy of this branch's scripts. Each subject gave one repair in 1.2–1.3 s of wall time, JVM included. The first adapter pass could not read three of them (`humanoid-458`, `humanoid-742`, `pcar-v2-888`), because the tools print `v!={}` for a variable they leave free. With the empty set read as no value, all ten translated, and `realize` reported all ten translated files `REALIZABLE`.

After the deduplication rule and the cap were added, JVTS-Repair ran on `lift` with the driver at `2212f3c7c209`. It finished by itself in 3.8 s of search with 221 distinct repairs from 805 raw, uncapped, and all 221 translated. With `max_repairs = 50` it stopped at `@@CAP 50 156`, exit 0, and its 50 repairs were the first 50 of the uncapped run, byte for byte.

## 9. Budget

GLASS costs seconds. JVTS-Repair and AMT13 take at most 20 jobs × 7200 s, which is 40 JVM-hours. At concurrency 10 that is two waves of at most 2 h, so the phase takes about 4 h of wall time on av2. The repair cap bounds the adapter at 30,000 TLSF files.

## 10. What follows

The screens (section 5, part 1), the N-tool overlap pass and the ε-separation recount are separate steps. They run once this campaign's rows are in. The paper then adds the three tools to RQ3's tables through `tables.py`, on the GR(1) subset only.
