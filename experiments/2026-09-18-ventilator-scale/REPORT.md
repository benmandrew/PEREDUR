# Deciding realizability on the ventilator and RAD specifications

Working notes written 2026-09-23 on branch `feat/rad-example` and committed unchanged on 2026-09-29, apart from this paragraph and the last sentence of *Open*. Every ventilator figure describes the 114-guarantee System + Controller import of 2026-09-18, which `examples/ventilator-controller` (68 guarantees, Controller only) has since replaced; guarantee indices do not carry over. RAD figures predate its conversion to ordinal ticks, which left its cores unchanged. Everything below was measured with the `counter-fretish-muc` `build-release` binaries (`realize`, `ltl`, commit `0cde635`) locally, and with `~/projects/counter/build-release` (commit `3cd6efb`, bundled SPOT 2.15.1) on av2 and av3. Constraint on the whole investigation: SPOT-family tools only, so no Kind2, JKind, jrealizability, Strix, Slugs or Spectra.

## The problem

Whole-specification `ltlsynt` never decides the mechanical lung ventilator. The import is 114 guarantees over 55 inputs and 107 outputs with no assumptions. Ten option variants were left undecided at 600 s, fifteen provisional repairs each hit a one-hour cap at 2.9–3.4 GB, and monolithic repair returned zero candidates after 1,599 s wall and 5,429 CPU-seconds. That blocks the output gate, so no repair can be confirmed at this size whatever the search budget.

## Routes ruled out

*FRET*'s own compositional realizability check does not help. Its `connected_components.js` groups requirements by intersecting output-dependency sets and writes one Lustre file per component, then hands each to `kind2 --enable CONTRACTCK` or `jrealizability`. Reimplementing that grouping on the ventilator gives 21 output components with the largest holding 79 outputs and 94 guarantees, and FRET's own per-component split has System and Controller writing 27 outputs in common, so it is not a sound interface split. No compositional flag reaches the solver. What makes FRET scale on these specifications is its engine, which the SPOT-only constraint excludes.

`ltlsynt` tuning does nothing. `--algo=sd|ds|ps|lar|acd` and `--decompose=no` were each left undecided at 400 s, and `--splittype=expl` timed out at 300 s. `--decompose` reports "there are 1 subformulas" on the residual, because the specification's outputs are fully coupled.

Compositional automaton *product* does not help either, and the negative result is informative. Folding the 29 residual conjuncts pairwise with `autfilt --product --small --high`, smallest first, gives 252 then 700 then 1,425 states and stalls past 600 s on the thirteenth. Bisimulation reduction after every multiply collapses nothing. The blow-up is genuine state distinguishability rather than an artefact of translating the conjunction in one go.

## Sound decomposition

An automated decomposition reduces the ventilator to one hard block. Three ingredients: *polarity constant-fixing*, where an output occurring with a single polarity in the negation normal form of the lowered formula is fixed to a constant, which is exact in both directions; a *conjunctive normal form response split*, where `G(c -> (a & b))` becomes `G(c -> a) & G(c -> b)`; and a partition into blocks with disjoint write sets, where outputs owned by another block are read as free inputs.

The response split distributes for `Always`, `Immediately`, `NextTimepoint` and `ForTicks`. It does not distribute for `AfterTicks`, because that timing lowers to *not before, then at exactly N* and the negated copy blocks it, nor for `Eventually` or `WithinTicks`. Three of 114 guarantees mismatched before `AfterTicks` was removed from the distributing set; `ltlfilt -f A --equivalent-to B` caught it, and 561 guarantees across the corpus then verified with 71 split and 71 equivalent.

Reading foreign outputs as free inputs is a *strengthening*, so a REALIZABLE verdict on a block transfers to the whole only when the blocks remain a partition and same-step dependencies are acyclic. Delayed reads (`NextTimepoint`, `AfterTicks`) impose no same-step edge; `Always`, `Immediately` and `ForTicks` do. The UNREALIZABLE direction uses a separate check where every atom keeps its original input or output role, which makes the block a genuine guarantee subset.

On the ventilator this fixes 83 of 107 outputs and leaves 9 parts with 0 same-step cycles. Eight decide in under 6 s. A sound UNREALIZABLE comes back in 0.06 s from 3 calls.

Validation found one real soundness bug in the construction. Two blocks could each absorb the same synthetic part for an output owned by nobody, so both owned that output and their strategies could disagree, breaking write-set disjointness silently. A 35-specification corpus differential and 1,100 random specifications both missed it; the fuzzer found it on a four-output case where two blocks each claimed `ou3`. Blocks became a union-find partition so every output has exactly one owner at every step. After the fix: 35 of 35 corpus specifications agree with whole-specification `realize`, and 3,900 random cases show 0 unsound and 0 inconclusive verdicts.

## The residual

The ninth part is the mode machine: 29 conjuncts over 12 outputs and 28 inputs. Whole-part `ltlsynt` is undecided at 1,800 s.

Subset screening over it is cheap and exhaustive, which is where the leverage is.

| Subset size | Checks | Timeouts | New cores | Wall |
|---|---|---|---|---|
| 1 | 29 | 0 | 0 | 0 s |
| 2 | 406 | 0 | 3 | 3 s |
| 3 | 3,574 | 0 | 0 | 28 s |
| 4 | 22,726 | 0 | 2 | 188 s |
| 5 | 110,307 | 0 | 0 | 947 s |

Compare the same screen over all 114 guarantees, which costs 433 s for 233,915 triples and would need C(114,4) = 6.9 million checks for quadruples.

Every conjunct is trivial alone, with the largest automaton at 11 states, while the determinised conjunction triples per conjunct: 3 states at n=2, 2,225 at n=11, and `ltl2tgba -D` past 60 s shortly after. `ltlsynt --verbose` shows the obligation bypass failing and the run stalling in translation, never reaching the parity game. There is no cliff to route around.

## Cores

A core found in the reduced residual is not automatically a core of the original. Polarity reduction is exact for the whole specification, but a *subset* of a reduced specification is a strengthening of the corresponding original subset. Two of five residual cores were artefacts reading REALIZABLE on the original — `{86, 113}` and `{41, 45, 71, 113}` — both because the reduction had fixed `parameters_stored` and collapsed `F(parameters_stored & off)` to `F off`.

Three cores survive confirmation against the original specification with original variable roles.

| Core | Contradiction |
|---|---|
| `{45, 79}` | 79 forces `start_up_mode` at the next tick when the environment raises `power_button & breathing_circuit_connected & !patient_connected & air_supply_connected & power_connected`. 45 then requires `!patient_connected` while in `start_up_mode`, and `patient_connected` is an input. |
| `{79, 102}` | 102 requires `G !start_up_mode` from the moment `power_off & !power_button` holds. The environment triggers that at one tick and 79's condition at the next. |
| `{41, 45, 54, 84}` | 54 sends input `start_up_done` to eventually `new_patient \| resume_ventilation`; 84 and 41 send each branch to `self_test_mode`; 45 forbids `self_test_mode` while `patient_connected` holds. Both branches must be closed, so it does not shrink below four. |

All three are minimal, and every member is REALIZABLE as a singleton. The four-guarantee core is new and was out of reach of the earlier triple screen.

Guarantee 45 is the root cause, appearing in two of the three cores. Its response constrains `patient_connected`, an input, so no implementation satisfies it except by never entering `start_up_mode` or `self_test_mode`. RAD has the identical defect class: its core is guarantee 65, `potential_robot_user_collision -> collision_detected`, where both atoms are inputs and the system has no output in the requirement at all.

Clearing the cores does not restore monolithic decidability. Removing `{10, 16, 28}`, a minimal hitting set of all five residual cores, leaves 26 conjuncts that `ltlsynt` cannot decide in 1,800 s.

## Candidate-and-check synthesis

A refinement loop decides both directions where the monolithic call decides neither. Pick a subset *S* of the requirements; synthesise a controller *C* for *S* alone with `ltlsynt -H`; test *C* against each requirement outside *S* with `autfilt --included-in`; add the violated ones to *S* and repeat. It works because `L(φ₁ ∧ … ∧ φₙ)` is `⋂ L(φᵢ)`, so containment distributes over conjunction and the product automaton is never built. Each check runs against one requirement's automaton, at most 11 states here.

Soundness holds in both directions without further assumptions. If a subset is unrealizable then so is the whole, since any controller for the whole realises every subset. If *C* is input-complete and contained in every `L(φᵢ)` then `L(C) ⊆ L(Φ)`, and `ltlsynt` strategies are input-complete by construction. One alphabet subtlety: *C* is built over the propositions in *S* only, so propositions outside it are free in `L(C)`, and containment against a requirement mentioning an unseen proposition succeeds only if that requirement holds under every valuation of it. `G(c)` correctly fails containment and `G(c | !c)` correctly passes.

Termination is guaranteed in at most n rounds, because every non-returning iteration adds an index to *S*. Completeness is only relative to the synthesis oracle: with unbounded time the worst case degenerates to *S* = everything, which is the monolithic call, and under a budget the run can stall.

| Object | Monolithic | Candidate-and-check |
|---|---|---|
| Whole ventilator, 114 guarantees | never answers | UNREALIZABLE, core `{70}` |
| Residual with cores cleared, 26 conjuncts | TIMEOUT at 1,800 s | REALIZABLE with witness controller |
| Whole RAD, 74 guarantees | UNREALIZABLE | UNREALIZABLE, core `{65}` |

Two limitations are measured. The loop stalls on the whole ventilator once the cheapest core is removed: round 2 times out with *S* grown to 53 conjuncts, round 3 at 67, both at a 900 s synthesis budget. And it is sometimes worse than monolithic — on `lift-plus-cruise-full` it went STUCK after 53 iterations and 926 s where `realize` answers in 1.3 s. Neither technique suffices alone; the decomposition is what made the 26-conjunct residual tractable.

Corpus differential, `realize` against the loop, across the 12 FRETISH specifications: 7 agree, 1 stalls in the loop arm, and `liquid-mixer` fails in the *monolithic* arm with "invalid specification" under commit `3cd6efb` while the local `0cde635` binary accepts it. That binary vintage question is unresolved.

## Two specification defects found by the witness

The controller that proves the cleared residual realizable is one state, and it asserts every mode simultaneously: `self_test_mode & standby_mode & psv_mode & fail_safe_mode & start_up_mode & pcv_mode`, forever. The ventilator specification never states that its modes are mutually exclusive. That is a sound verdict with a useless witness.

Adding the 15 pairwise exclusivity constraints turns the remainder UNREALIZABLE, and the minimal conflict is a four-way one that exclusivity makes visible. Guarantee 4 sends `psv_mode & stop_ventilation` to `standby_mode` at the next tick; guarantee 10 sends `apnea` to `pcv_mode`; guarantee 18 sends `pcv_mode & psv_mode_selected` to `psv_mode`; and `G(!standby_mode | !pcv_mode)` forbids the first two holding together. The environment raises both triggers on the same tick and the specification demands two different next modes. The mode transition relation is not deterministic.

## Hot-path economics

Neither technique belongs in the per-generation gate, and the reason is subprocess cost rather than algorithmic cost. Measured on av2 and av3 with SPOT 2.15.1, 200 iterations per figure for the fork costs and 20 for `realize`:

| Call | Cost | Of which fork+exec |
|---|---|---|
| `/bin/true` | 0.51 ms | 0.51 ms |
| `ltl2tgba --version` | 1.59 ms | 1.59 ms |
| `autfilt --version` | 7.65 ms | 7.65 ms |
| `ltlsynt --version` | 7.87 ms | 7.87 ms |
| `autfilt --included-in`, 1 conjunct | 8.23 ms | 7.65 ms |
| `ltlsynt --realizability`, 1 conjunct | 9.12 ms | 7.87 ms |
| `realize examples/fsm/spec.json` | 11.35 ms | 7.87 ms |
| `realize examples/mode-arbiter/spec.json` | 9.47 ms | 7.87 ms |

`realize` on a small specification makes exactly one `ltlsynt` fork, confirmed by `strace -f -e trace=execve`, so roughly 80% of a gate call is process startup. `autfilt` and `ltlsynt` cost the same per call to within 11%. Replacing one gate call with *k* containment checks is therefore a wash at *k* = 1 and a 74% regression at *k* = 2. Incremental validation against a cached parent controller is dead on those grounds, and the batcher result — batch size 1.012, 1.9% fewer execs, 37% slower on 46 of 46 pairs — says fork cost is a known constraint that has already resisted one attempt to amortise it.

One monotonicity consequence survives, because it removes the subprocess rather than replacing it. If *C* realises Φ and Φ′ is weaker then *C* realises Φ′, since `L(C) ⊆ L(Φ) ⊆ L(Φ′)`. Every weakening descendant of a confirmed repair is free, with no call at all, which fits the accumulator directly.

For the gate itself the recommended shape is a *staged race*: give `realize` a head start of 1–2 s, and launch the refinement loop alongside it only if the head start expires. On the corpus all 7 specifications that `realize` decides do so within 1.3 s, so the head start covers every one at zero extra CPU; the ventilator needs the second arm; and `lift-plus-cruise-full` never launches it, which avoids the single case where the loop is catastrophic. A plain race would start that 926 s arm and cancel it. Racing in the per-generation gate is the wrong trade regardless, because cancellation refunds nothing already spent and the population already saturates every worker, so doubling CPU per call doubles the run. The serial phases — the output gate and the final realizability collect — are where cores sit idle and a race is close to free.

Two integration hazards. Cancelling the loop arm means killing a tree of `ltlsynt`, `autfilt` and `ltl2tgba` children, and this codebase already needed a janitor sweeping `ppid == 1` at age over one hour for leaked multi-gigabyte `ltl2tgba` processes, so the arms want `setpgid` and `killpg` rather than killing the direct child. And `max_wall_s` bounds the search only, with the gate already running past the deadline at roughly 2.2× the cap, so a loop arm that stalls for 926 s inside the gate needs both a hard iteration cap and its own wall cap.

## Literature

No published paper matches this loop — refinement over requirement subsets, with per-conjunct language containment of a synthesised Mealy machine as the candidate check. Searches across Crossref, OpenAlex, Semantic Scholar and dblp found none.

The nearest neighbour is Maoz and Shalom, "Unrealizable Cores for Reactive Systems Specifications", ICSE 2021, DOI `10.1109/ICSE43902.2021.00016`. Their QuickCore searches guarantee subsets exploiting the same monotonicity of unrealizability, and Punch computes all unrealizable cores; each subset is tested by a GR(1) realizability check rather than by containment-checking a controller synthesised from a smaller subset. The generic pattern is counterexample-guided inductive synthesis (CEGIS), Solar-Lezama, Tancau, Bodik, Seshia and Saraswat, "Combinatorial sketching for finite programs", ASPLOS 2006, DOI `10.1145/1168857.1168907`.

Finkbeiner and Jacobs, "Lazy Synthesis", VMCAI 2012, LNCS 7148, pp. 219–234, DOI `10.1007/978-3-642-27940-9_15`, is *not* this algorithm: it refines a candidate implementation and its constraint system, derived from bounded synthesis, rather than refining over requirement subsets. The similarly-named Finkbeiner and Schewe, "Bounded Synthesis", STTT 15(5–6):519–539, 2013, is a separate paper.

Other directly relevant work, all Crossref-verified. Katis, Mavridou, Giannakopoulou, Pressburger and Schumann, "Capture, Analyze, Diagnose: Realizability Checking of Requirements in FRET", CAV 2022, DOI `10.1007/978-3-031-13188-2_24`, covers minimal-conflict diagnosis over FRETISH requirement sets. Finkbeiner, Geier and Passing, "Specification Decomposition for Reactive Synthesis", NFM 2021, DOI `10.1007/978-3-030-76384-8_8`, is what `ltlsynt --decompose` implements. Filiot, Jin and Raskin, "Antichains and compositional algorithms for LTL synthesis", *Formal Methods in System Design* 39(3):261–296, 2011, DOI `10.1007/s10703-011-0115-3`, computes a winning-region antichain per conjunct and composes them. Bansal, De Giacomo, Di Stasio, Li, Vardi and Zhu, "Compositional Safety LTL Synthesis", VSTTE 2022, DOI `10.1007/978-3-031-25803-9_1`, synthesises per conjunct and composes, sound and complete for Safety LTL only. Kupferman, Piterman and Vardi, "Safraless Compositional Synthesis", CAV 2006, DOI `10.1007/11817963_6`, reuses emptiness-check work across components rather than synthesising on a subset. For core extraction see also Cimatti, Roveri, Schuppan and Tchaltsev, "Diagnostic Information for Realizability", VMCAI 2008, DOI `10.1007/978-3-540-78163-9_9`, and Könighofer, Hofferek and Bloem, "Debugging formal specifications using simple counterstrategies", FMCAD 2009, DOI `10.1109/FMCAD.2009.5351127`.

SPOT documents nothing of this kind. `ltlsynt`'s only decomposition-flavoured options are `--decompose`, `--global-equivalence`, `--polarity`, `--bypass`, `--obligation-synthesis`, `--algo`, `--splittype` and `--simplify`, and Renkin, Schlehuber-Caissier, Duret-Lutz and Pommellet, "Dissecting ltlsynt", *Formal Methods in System Design*, 2022, DOI `10.1007/s10703-022-00407-6`, records no compositional or incremental mode beyond decomposition.

## Harness bugs worth not repeating

Four defects in the scratchpad harness, each caught by a differential rather than by inspection, which is the argument for keeping the differential.

**Mode atoms as outputs.** `environment_signals()` at `src/requirement.cpp:455` concatenates `m_in_atoms` with `m_modes`, so mode atoms are uncontrollable, and `include/requirement.hpp:218` explains why: a mode on the output side lets the synthesised system choose its own scope, giving every FRETISH scope a free gutting move. Passing only `in_atoms` to `--ins` handed the controller `degraded` and `maintenance` on `mode-arbiter`, and `detection_mode`, `dressing_mode` and `task_execution` on RAD. This only ever adds outputs, so UNREALIZABLE verdicts and the RAD core survive it, while any REALIZABLE verdict under it is worthless.

**Assumptions leaking into conjuncts.** Building a one-guarantee specification without clearing `assumptions` makes `ltl` emit the assumption line first, and a parser taking the first `LTL:` line gets the assumption. All three `mode-arbiter` conjuncts were byte-identical copies of it. `Specification::to_ltl()` is `(⋀A) -> (⋀G)`, which distributes as `⋀ᵢ ((⋀A) -> gᵢ)`, so the fix is to fold the assumptions into every conjunct and containment still applies.

**Index misalignment.** `ltl` on a multi-guarantee specification prints fewer `LTL:` lines than there are guarantees — 110 for the ventilator's 114, 29 for the residual's 32 — so positional zipping silently misattributes every core. Generating one conjunct per call gives a 1:1 map.

**Invalid measurements from a missing corpus.** The examples directory was rsynced to av2 only, so `realize` benchmarks on av3 measured a failed file open at 1.3 ms and appeared to show zero subprocess calls. Any benchmark of an external binary should assert on its output before timing it.

## Open

RAD's iterated loop on av3 and the last corpus rows are still running. The `liquid-mixer` monolithic failure under commit `3cd6efb` is unexplained. Whether the loop still proves REALIZABLE cheaply once mode exclusivity is enforced, so the controller has to be a real state machine rather than a one-state constant, is unmeasured — and that result decides whether the technique scales or merely got an easy case. The harness lived in a session scratchpad and was not kept.

The decomposition and the refinement loop each fail where the other works, which is a more useful outcome than either succeeding alone would have been. What neither addresses is that the ventilator's real problem is two modelling errors — a guarantee constraining an input, and absent mode exclusivity — that a realizability checker can only report as a contradiction.
