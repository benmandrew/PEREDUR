# Mutation and crossover operators

The breeding grammar on both front ends, what each operator is for, and the measurements behind each change to it.

## Crossover

Both crossovers follow AuRUS's `SpecificationCrossover` (`~/projects/tools/aurus/src/geneticalgorithm/SpecificationCrossover.java`, level ≥ 2) and its `FormulaUtils.replaceSubformula` / `combineSubformula`. Per side, *exactly one conjunct is drawn from each parent, uniformly and independently, and merged*: with probability 1/2 a subformula of the first is replaced by one from the second, otherwise the two are joined under a fresh binary operator. Every other slot is the first parent's, and no branch copies a field verbatim, so a crossover that fires always recombines.

The donor's slot is unrelated to the target's, so material moves between slots. The offspring keeps the first parent's shape, so only the in/out atoms (FRETISH) or signals (TLSF) must match; an equal-length guard would stop any individual that gained an assumption from breeding. The merge is written back in place rather than appended as AuRUS does, because slot *i* of a candidate must keep descending from slot *i* of the original (see "Removable guarantees").

Deleted and non-weakenable requirements are never target or donor. AuRUS's `≤ 2 temporal operators` rejection is dropped, the bloat cap doing that job. On TLSF a conjunct with no temporal subformula offers itself as its graft site, since INITIALLY, PRESET, REQUIRE and ASSERT are routinely propositional and crossover would otherwise be a no-op over most of a specification. FRETISH fields are propositional, so that path joins under ∧, ∨, →, ↔ rather than ∧, ∨, U, W.

The condition type and the scope are each taken whole from one parent on a coin after the timing's draw: two ungated draws per crossover. `test_generation_draw_sequence_is_pinned` holds the stream at 187 draws. The scope keeps its mode, which is sound because `crossover_specifications` refuses parents whose declared modes differ.

## Operator repairs of 2026-08-19

`experiments/2026-08-14-aurus-h2h/REPORT.md` measured defects in the grammar against 2,295 archived repairs; the seven fixes below are unconditional. The `[genetic] repaired_operators` key and the legacy paths were deleted, so restoring old behaviour means reverting the commit.

The evidence is `experiments/2026-08-20-ops-grammar` and `experiments/2026-08-20-ops-weakening` (720 paired cases, `REPORT.md` in the latter). With the weakening screen off, as now ships, the pooled endpoint is null on both paths and the pre-registered rule said do not flip. The decision rests instead on reachability: with the screen on, `minepump` reads 0/20 under the legacy grammar against 12/20, while `humanoid-531`, which builds much larger formulae, loses runs to the repairs. Sweep O and the `ops-*`/`opswk-*` profiles are retired; those archives reproduce only through their vendored `scripts/`.

`src/prop_formula/simplify.cpp` carries ungated folds: `G G φ → G φ`, `F F φ → F φ`, the U/W/R self-joins, and the boolean constants through every temporal operator (`φ W false ≡ G φ` and `false R ψ ≡ G ψ` being the two that do not annihilate). The constant folds exist because the `Constant` monotone rewrite left spellings such as `G(false)` standing, and 43.7% of formulae reaching `ltlsynt` mentioned a constant. They do not change wall time (paired A/B over 33 cases, sign p = 1.000), never fire on FRETISH, and stay because a guarantee folding to `true` is gutted whether or not anything notices. Both ops campaigns ran with them in both arms and attribute nothing to them.

- **An atom can grow.** `mutate_atom_formula` draws a third move beside rename and negate: graft a drawn anchor, with drawn polarity, onto the atom under a connective. Otherwise guarding a positive literal takes three chained draws; without the polarity draw `minepump`'s ideal `high_water & !methane` stayed at 1 of 20. An empty atom pool keeps the two-move draw.
- **The temporal path can preserve a connective.** `mutate_temporal` case (3) has an arm keeping the node's kind and mutating its children, so an implication is not reachable only to be destroyed.
- **An assumption obliges an input.** `tlsf_add_assumption` draws the consequent from the inputs alone, since an assumption obliging an output can be defeated by the system withholding it; the guard may draw an output. The consequent's modality is drawn from {F, X, nothing}, and a guard identical to its consequent has its polarity flipped.
- **Sections are not sides.** An INITIALLY or PRESET slot draws from its own signals, takes only the propositional rewrite, and accepts a donor only from its counterpart section, as Basic TLSF requires. The atom pool is per-section; the bloat cap's baseline is not.
- **One occurrence gives way.** TLSF crossover's graft replaces the first occurrence of the chosen subformula, not every one.
- **The FRETISH graft site is uniform.** `replace_subformula` and `combine_subformula` draw the site uniformly, since a coin per node reaches the k-th with probability 2⁻ᵏ and grafts nowhere with 2⁻ⁿ. `mutate_atom_name` has its TLSF twin's distinctness guard.

The bloat cap keeps its specification-wide baseline. A per-section baseline tightens it where a weakening needs room, raising `arbiter-aurus`'s drop rate from 1.6% to 9.4% and costing 9 of its 10 ideal-implying runs. Watch for a filter tightening that fights an operator loosening.

`tlsf_mutate` draws its slot before the atom pool, and TLSF `cross_side` draws its target before testing the donor pool, because the pool follows the slot's section. The FRETISH goldens do not cover the TLSF path, so reordering either is silent.

## Implication in the TLSF grammar

`pick_binary_kind` (`src/tlsf/mutation.cpp`) draws from {∧, ∨, →, U, R, W}. Without `Implies` (Brizzio's negation normal form fragment has none), `ltl2dba-r-2`, `ltl2dba-theta-2` and `ltl2dba27` are unreachable, each ideal replacing a root `<->` with `->`, which `mutate_temporal` case (3) reaches once the draw includes it. The FRETISH twins in `src/genetic/mutation.cpp` and `src/genetic/crossover.cpp` draw `{And, Or, Implies, Iff}`, so this is TLSF-only. `Iff` stays out: the defect was that a biconditional could not be weakened, and nothing supports manufacturing them.

`pick_connective_kind` draws `next_index(5)` over {U, W, ∧, ∨, →} for case (2d), which grafts an anchor onto the mutated child at an atom or unary node. `Implies` there makes `p -> X phi`, the shape of every minimal guarantee weakening, one draw away where a guard is introduced. The anchor goes first, giving `anchor -> inner`; `Release` is left to `pick_binary_kind`. The `connective_implies` key was removed, so no key restores the old TLSF stream.

## Monotone rewrites

`monotone_rewrite` (`src/genetic/monotone.cpp`, `include/genetic/monotone.hpp`) serves both front ends under `[mutation] p_monotone` (`Config::p_monotone`, default 0.25). On TLSF it is a rewrite arm offered ahead of the temporal and propositional ones; on FRETISH it is offered inside `p_response` and `p_trigger`, so it is inert unless one is above 0. It lives in the genetic layer because `src/tlsf/mutation.cpp` includes `genetic/mutation.hpp`, never the reverse, which is also why `draw_literal` is in `include/genetic/mutation.hpp`. `Weaken` returns a formula the parent implies, `Strengthen` one implying the parent.

The general rewriters leave the implication order, changing AST shape 93.6% of the time. Rescored through PEREDUR's maximality filter over the shared families, PEREDUR reads `best_relation` incomparable on 37.5% of runs against AuRUS's 22.7% (`maximality_rescore` in `experiments/2026-08-14-aurus-h2h/PROVENANCE.json`, script `scripts/aurus_maxscore.py`). Two of AuRUS's three mutation visitors are monotone, against which 0.25 is a conservative first value; no campaign has tuned it.

**Polarity is what makes the guarantee hold through nesting.** A weaker subformula weakens the whole only at positive occurrence, so `Not` and an `Implies` antecedent flip direction and an `Iff` child is not a rewrite site. Site and rule are drawn uniformly over nodes with determinate polarity and rules applicable there. A new rule goes into `rules_at` or `extra_rules_at` under its node kind *and* its direction; a misfiled one fails `test_monotone_rewrite_direction_holds`.

The FRETISH arm applies this to the lowering as a three-entry table in `src/genetic/mutation.cpp`. `scope_wrap` and both condition wrappers place `body` positively, so the response takes the requirement's direction, flipped under an `only` scope (its lowering applies the dual wrapper to `negated_timing_body`, putting the response under a `Not`) and sat out under `after n ticks`, where it occurs at both polarities. The condition takes the flipped direction under a continual condition type and is sat out under a trigger, whose rising edge `(!c & Xc)` holds both polarities. `test/genetic/monotone_arm_tests.cpp` (ctest `peredur_tests.fretish_monotone`) checks the table against a solver; dropping any of the three guards fails it.

On TLSF the direction is a fair coin: a search that only weakens guarantees and strengthens assumptions cannot recover from an ancestor that overshot the ideal, and the arm's purpose is comparability. The FRETISH arm follows the requirement's mutation direction, as its timing, condition-type and scope arms do.

The probability is read before the RNG is drawn, so at 0 the arm costs no draw. `test_zero_probability_costs_no_draw` (`test/tlsf/monotone_tests.cpp`) pins TLSF; `test_new_arms_cost_no_draw_at_zero` (`test/genetic/determinism_tests.cpp`) pins FRETISH by comparing rendered draw traces, since a replacement rewrite can draw equally often down another branch. `golden_config()` pins the key at 0, and the key is in the "Config vintage" note in `experiments/README.md`.

`AddOperand` and `Constant`, the two rules sound at every node, are offered everywhere. Limiting `AddOperand` to `And` and `Or` would leave `Constant` as the only move at a literal, while every assumption-shaped ideal is a disjunction of literals. The connective comes from the direction, not the node kind.

Five extra rules fill the `Release`, `Next` and `Iff` menus: `phi R psi` weakens to `psi` and strengthens to `G psi`; `X phi` weakens to `F phi` and strengthens to `G phi`; `a <-> b` strengthens to `a & b` or `!a & !b` on a coin. `rules_at` appends `extra_rules_at`'s result. Both widenings are unconditional, so the signature is `monotone_rewrite(formula, direction, atoms, random_source)`. `test_monotone_rewrite_direction_holds` includes `Release` and `Next` subjects, checking every rule against `black`.

## Cloned assumptions

`tlsf_add_assumption` emits at most 7 nodes, while assumption-shaped ideals are far larger (`gyro-var2`'s is about 29). Under `[tlsf.mutation] p_clone_assumption` (default 0.25) the operator instead appends a copy of a live ASSUME conjunct for later mutation to edit. PEREDUR's crossover draws one conjunct per side and cannot union subsets as AuRUS's level-1 crossover does, so the move belongs to mutation.

Only ASSUME is drawn from, since copying INITIALLY or REQUIRE into ASSUME changes its meaning. Tombstones are skipped, and with nothing live the template stands in. The template keeps the majority of the draw as the only form that introduces a new fairness property.

The probability is read before the `RandomSource` is touched, so at 0 it costs no draw; the key is in the "Config vintage" note.

## Assumption construction

Two keys remain, each at a no-op default (campaign-armed value in brackets):

- `[tlsf.mutation] max_assumption_width` (1, armed 3): an appended body is drawn from `term := [F](literal & ... & literal)`, `body := term | ... | term`.
- `[tlsf.mutation] p_bare_assumption` (0.0, armed 0.25): an unconditional assumption may be `F body` rather than `G F body`.

Each is read before the `RandomSource` is touched, pinned per key in `test/tlsf/assumption_tests.cpp`, and needs no "Config vintage" entry, no default having moved.

The campaign is `experiments/2026-08-26-assumption-reach` (sweep U). Its primary is null (exact McNemar p = 1.0000). The keys stay for two reaches: `lift` repaired for the first time, 3 of 12 under `reach-l`, its ideal `G F (b1 || b2 || b3)` being a width-3 body, and `lily11` rising from 5 to 11 of 12 through `p_bare_assumption`, its ideal a bare `F req`.

Three keys are removed. `[tlsf.crossover] p_union_assumption` is unreachable by construction: `gyro-var2`'s ideal is a conjunct of `gyro-var1`'s specification and vice versa, material in a sibling no run sees, and arming it displaced clone-and-edit. `[tlsf.mutation] p_burst_continue` measured a loss (p = 0.0226), PEREDUR already mutating more aggressively than AuRUS's `Poisson(1)`; mutation applies exactly one edit. `[tlsf.mutation] p_remove_assumption` had no reach attributed. Sweep U and the `assumption-reach` profile are retired.

## Timing donation

The extremes of the FRETISH timing order move only into a timing the specification donates. `collect_timing_pool` gathers distinct timings from both lists, skipping tombstones, and `donated_candidates` (`src/genetic/mutation.cpp`) builds the candidates: a quantified donor lends its tick count as `for n ticks`, Immediately and NextTimepoint lend themselves. With no donor the extreme is unchanged.

Everything in the set lies strictly between Always and Eventually, so the same candidates strengthen one and weaken the other through the shared `move_off_extreme`. A donor's kind is never taken: `within n` respells one count and `after n` is not comparable to Always.

No invented constant: a hard-coded `for 10 ticks` weakening of Always produced a degenerate `fsm` repair, and freezing Always (`f4968ab`) left the timing of every Always guarantee immobile, 7 of the 9 across the three FRETISH input specs.

A weaker guarantee timing helps realizability and costs nothing on similarity, so under NSGA-II a gutted timing sits on the front; that wants a campaign.

`move_off_extreme` reads the pool before touching the `RandomSource`, so an empty pool costs no draw, and no golden specification has an Always guarantee.

## Removable guarantees

`p_remove_guarantee` (default 0.05, matching `p_add_assumption`) deletes one FRETISH guarantee or one TLSF conjunct from PRESET, ASSERT and GUARANTEE. Some ideals need it: amba, full-arbiter, load-balancer, prioritized-arbiter and round-robin-arbiter have only `drop-*` ideals. `lint-ideals`' `reachable` check asserts a guarantee side may shrink, never grow, to a floor of one.

A deleted guarantee is *tombstoned in place, never erased* (`Requirement::m_removed`, `tlsf::SectionEntry::m_removed`), because specifications pair by position: `average_timing_similarity` and both semantic-similarity folds zip by index and crossovers write back into the source slot.

The flag is in `operator<`, `operator==` and `hash`, so a deleted guarantee cannot inherit a live one's cached fitness. Every requirement-list reader skips tombstones: LTL lowering, status components and the MRS index map, vacuity, well-separation, the bloat cap, `all_implied_by_some`, the timing, mutation and simplification pools, MUC extraction and the guarantee-part split. Crossover never draws a tombstone, so removal is mutation's alone. `tlsf::SectionEntry` converts implicitly from `Formula` but not back, so a new read site fails to compile.

`to_json` and `tlsf::write` omit tombstones, so a written repair carries no flag to misread.

The probability is tested before the RNG is drawn, so at 0 it costs no draw. Archived configs predate the key, so reproduce them with `p_remove_guarantee = 0.0` (see "Config vintage"). `golden_config()` pins the key.

Deletion helps realizability and costs nothing on similarity, so under NSGA-II a gutted candidate sits on the front; TLSF sweep D measures that, with `prem0` as control.
