#pragma once

/// @file status.hpp
/// @brief Realizability status fitness score, on the three-point scale shared
///        by the FRETISH and TLSF front ends.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "config.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"

/// Whether a status score tests its own components before grading, or takes
/// that tier as already decided.
///
/// Skipped exists for the split scoring path, which runs each component's
/// satisfiability query as a dispatch item of its own and applies the tier
/// itself. Testing them twice would either duplicate the queries or serialise
/// them behind the score that guards on them, and the two are the same tier
/// either way.
enum class ComponentCheck : std::uint8_t { Included, Skipped };

/// Some component formula of the candidate is unsatisfiable on its own.
inline constexpr double k_status_component_unsatisfiable = 0.0;
/// Every component is satisfiable, but the candidate is not realizable for a
/// reason worth anything: either no strategy exists, or the only strategy is to
/// defeat the candidate's own assumptions. See status_score on why those two
/// share a tier rather than being graded apart.
inline constexpr double k_status_unrealizable = 0.5;
/// The candidate is realizable, and well-separated with it.
inline constexpr double k_status_realizable = 1.0;

/// Scores a candidate on the shared status scale, given its component formulae
/// and a way to decide realizability. Both front ends route through this, so
/// the scale lives in one place; only the decomposition into components and
/// the realizability query differ between them.
///
/// The scale is deliberately coarse. Earlier revisions graded the region below
/// realizability more finely -- separate tiers for an unsatisfiable guarantee
/// side and an unsatisfiable assumption side, mirroring the design this search
/// is derived from. Instrumenting both front ends over 887k scored candidates
/// found those tiers hold under 0.32% of the population between them.
///
/// The two failed for different reasons. The guarantee-side tier was measured:
/// nothing filters a jointly unsatisfiable guarantee side, and it fired on 46
/// candidates out of 15,115 on the TLSF path, all on one specification. The
/// assumption-side tier never fired at all, and could not have: its predicate
/// is exactly the vacuity filter's, and candidate filters run on offspring
/// *before* those offspring are scored, so such a candidate is discarded
/// before it reaches this function. The vacuity filter always runs, so the
/// tier was unreachable -- it re-asked a question a filter had already acted
/// on.
///
/// Grading *within* unrealizability needs a measure of how far a candidate is
/// from realizable, which a satisfiability query cannot express.
///
/// The top tier means realizable *for a real reason*: both callers fold
/// well-separation into @p is_realizable, so a candidate the system can satisfy
/// only by forcing its own assumptions to fail scores k_status_unrealizable
/// rather than k_status_realizable. That is a correction to what the top tier
/// meant, not a fourth tier. A tier between the two would rank cheating above
/// failing, and ill-separation is cheap for mutation to reach and expensive to
/// leave, so the intermediate score would be a broad plateau that the search
/// settles on -- the shape the aggregate fitness already shows for gutted,
/// vacuous repairs. Scoring it level with unrealizable keeps the candidate in
/// the population as breeding material, which is the whole value of not simply
/// dropping it, and pays it nothing for the cheat.
///
/// Reachable only because well-separation no longer runs as a pre-scoring
/// filter by default. A tier whose predicate a filter has already acted on is
/// unreachable by construction, which is what happened to the assumption-side
/// tier above.
///
/// @param components    Formulae to test individually; any one unsatisfiable
///                      scores k_status_component_unsatisfiable
/// @param sat           Satisfiability checker (black)
/// @param is_realizable Consulted only once every component is satisfiable, so
///                      a caller may fold in a cheap shortcut for candidates
///                      whose realizability is known without a solver call, and
///                      both fold in the well-separation query behind the
///                      realizability one so it is asked only where it can
///                      change the answer
/// @return              One of the three k_status_* constants
double status_score(const std::vector<std::string>& components,
                    SatisfiabilityChecker& sat,
                    const std::function<bool()>& is_realizable);

/// Only the guarantee side is satisfiable: the candidate's assumptions cannot
/// hold at all. See @ref status_score_aurus.
inline constexpr double k_status_aurus_guarantees_only = 0.05;
/// Only the assumption side is satisfiable. It sits above the tier opposite
/// because AuRUS ranks it so, an unsatisfiable guarantee side being the half a
/// repair is allowed to rewrite.
inline constexpr double k_status_aurus_assumptions_only = 0.10;
/// Both sides are satisfiable alone and contradict one another.
inline constexpr double k_status_aurus_contradictory = 0.20;

/// Scores a candidate on AuRUS's status ladder, selected by
/// Config::status_grading. It exists so an ablation can cross PEREDUR's own
/// grading against the design this search is derived from, which means
/// reproducing the ladder value for value rather than improving on it:
/// `getStatusFitness` and `compute_status` in
/// `AutomataBasedModelCountingSpecificationFitness.java`, AuRUS 3f6f01f.
///
/// The six levels, in the order the queries are asked:
///
///   - both sides unsatisfiable, k_status_component_unsatisfiable (BOTTOM);
///   - @p assumptions unsatisfiable alone, k_status_aurus_guarantees_only;
///   - @p guarantees unsatisfiable alone, k_status_aurus_assumptions_only;
///   - the two contradicting each other, k_status_aurus_contradictory;
///   - jointly satisfiable but not realizable, k_status_unrealizable;
///   - realizable, k_status_realizable.
///
/// Both side queries are asked whatever the first answers, as AuRUS asks them:
/// the second is what separates BOTTOM from the tier above it, and after the
/// first candidate carrying a side it is a lookup in SatisfiabilityChecker's
/// cache. The joint query is asked only where the two sides survive on their
/// own, so it costs nothing on the candidates the ladder has already placed.
///
/// An undecided *satisfiability* query scores k_status_component_unsatisfiable:
/// AuRUS leaves the status UNKNOWN and `getStatusFitness` pays UNKNOWN what it
/// pays BOTTOM. An undecided *realizability* query is the caller's to resolve
/// and lands on k_status_unrealizable, since @p is_realizable answers false.
/// The two differ in AuRUS as they differ here.
///
/// **This scale must not fold well-separation into @p is_realizable**, which is
/// the one place it diverges from every other scale in this file. AuRUS never
/// asks the question: `WellSeparationAnalysis.java` has no caller anywhere in
/// its `src`, so a candidate realizable only by forcing its own assumptions to
/// fail scores full marks there. An arm that folded the query in would grade
/// something AuRUS does not, and would no longer measure what it is named
/// after. Nothing about output correctness changes with it -- the final gate
/// asks the whole correctness table unconditionally, whatever the search scored
/// on.
///
/// There is no per-component tier here, deliberately: AuRUS tests each side as
/// one conjunction, so a candidate holding one incoherent requirement among
/// coherent ones is placed by the side queries rather than dropped to the floor
/// by a test AuRUS does not make.
///
/// @param assumptions   The assumption side as one LTL formula (`true` when the
///                      side is empty)
/// @param guarantees    The guarantee side as one LTL formula
/// @param sat           Satisfiability checker (black)
/// @param is_realizable Consulted only once both sides and their conjunction
///                      are satisfiable, and *without* the well-separation
///                      query the other two scales fold in
/// @return              One of the six levels above
double status_score_aurus(const std::string& assumptions,
                          const std::string& guarantees,
                          SatisfiabilityChecker& sat,
                          const std::function<bool()>& is_realizable);

/// Decides whether the guarantee-side parts at the given indices are jointly
/// realizable against the full, unchanged environment side. Indices address the
/// caller's own part list, so both front ends share the walk below without
/// sharing a representation of a specification.
///
/// The oracle must fold well-separation in behind realizability, as
/// @ref status_score's `is_realizable` does, and must resolve an undecided
/// query as false, matching how status_score treats an unanswered
/// satisfiability check: a timeout must not promote a candidate above one that
/// was decided.
using SubsetRealizability =
    std::function<bool(const std::vector<std::size_t>& indices)>;

/// Scores a candidate on the greedy maximal-realizable-subset (MRS) scale,
/// selected by Config::status_grading. Below realizability the score is the
/// fraction of the guarantee side that can be kept, which grades a region
/// @ref status_score collapses to one value: over the 21 specifications in
/// `examples/` the tiered score reads 0.5 for every one, where this spreads
/// them over 14 distinct values.
///
/// The walk admits parts in @p admission_order, taking each one whose addition
/// leaves the accumulated subset realizable. Order matters, since greedy
/// returns a maximal subset rather than a maximum one, so the score is a lower
/// bound on the true MRS. An empty order means index order, which is what the
/// walk shipped with; Config::mrs_admission_order selects what the front ends
/// pass instead.
///
/// Any order works here so long as it is the *same* order for every candidate
/// in a run, which is what keeps the score a deterministic function of the
/// candidate -- the seed reproducibility the `determinism` suite pins requires
/// that, and nothing about index order in particular. It is also what keeps
/// RealizabilityChecker's cache: two candidates differing only at part m ask
/// identical queries at every step before m's position, whatever permutation
/// puts it there. Measured over populations of mutants, every fixed order costs
/// 1.02x to 1.07x index order's ltlsynt execs, against 1.48x for a fresh order
/// per candidate.
///
/// The accumulated indices are kept sorted, so a set of parts reaches the
/// oracle in one order whatever sequence the walk admitted them in. Both front
/// ends build their subset in the order they are handed, so without this the
/// cache would key on the admission sequence rather than on the set.
///
/// Well-separation belongs *inside* @p subset_realizable rather than as a
/// post-hoc cap on a full-marks score. Folding it in costs an ill-separated
/// candidate the part that made it so, and leaves 1.0 meaning what it means on
/// the tiered scale -- realizable for a real reason. Capping instead would need
/// a value just below 1.0, which would rank a candidate that games its own
/// assumptions above one honestly missing a single guarantee, the ordering the
/// tiered scale's notes above refuse.
///
/// The component test is unchanged and runs first, so
/// k_status_component_unsatisfiable still means what it did. That tier earns
/// its keep here more than on the tiered scale: it costs satisfiability queries
/// rather than synthesis ones, and it fires exactly where the greedy walk would
/// otherwise pay @p n_parts realizability queries to learn nothing.
///
/// An empty guarantee side scores k_status_realizable, matching the shortcut
/// both front ends already apply: no guarantees leaves the implication with a
/// `true` consequent, which is realizable whatever the assumptions say.
///
/// @param components        Formulae to test individually, as for status_score
/// @param n_parts           Number of guarantee-side parts the oracle addresses
/// @param sat               Satisfiability checker (black)
/// @param subset_realizable Realizability of a subset of those parts
/// @param admission_order   Order to admit parts in, projected onto @p n_parts
///                          by @ref project_admission_order; empty means index
///                          order
/// @return                  k_status_component_unsatisfiable, or |kept|/n_parts
///                          in [0, 1]; 1.0 exactly when every part is kept
double status_score_mrs(const std::vector<std::string>& components,
                        std::size_t n_parts, SatisfiabilityChecker& sat,
                        const SubsetRealizability& subset_realizable,
                        const std::vector<std::size_t>& admission_order = {});

/// @p reference restricted to a walk over @p n_parts parts: entries addressing
/// a part that no longer exists are dropped, and parts the reference does not
/// cover are appended in index order.
///
/// An order is computed once on the input specification and replayed on every
/// candidate, and mutation moves the part count either way -- a rewritten
/// formula splits into a different number of conjuncts on the TLSF path, and a
/// removed guarantee shortens the walk on the FRETISH one. Projection makes the
/// order total for any @p n_parts rather than leaving the walk to guess, and an
/// empty @p reference projects to index order.
std::vector<std::size_t> project_admission_order(
    const std::vector<std::size_t>& reference, std::size_t n_parts);

/// Parts ordered by ascending pairwise-conflict degree: how many other parts a
/// part cannot be held together with, counted over every pair. Ties keep index
/// order, and a part that is unrealizable on its own sorts last, since no walk
/// can ever keep it.
///
/// This is the min-degree heuristic from constraint satisfaction, and it exists
/// because index order is measurably biased by one structure. Where a single
/// early part conflicts with the rest of the guarantee side, index order admits
/// it first and rejects everything it blocks: on `detector` that keeps 1 part
/// of 7 where deferring it keeps 6. Over populations of mutants across six TLSF
/// specifications this order scores 0.587 against index order's 0.529, at 1.02x
/// the ltlsynt execs, and it scored no lower than index order on any of them.
///
/// Costs n(n-1)/2 + n subset queries, dispatched over the global thread pool:
/// 28 on a 7-part specification, 136 on a 16-part one. That is paid once, by
/// the front end building its fitness function, against a run that scores tens
/// of thousands of candidates.
///
/// Call it from outside the pool. It submits to global_thread_pool() and blocks
/// until those tasks finish, so calling it from a pool worker would wait on a
/// slot that worker is itself holding. Both front ends call it while building
/// their fitness function, which happens on the main thread before any scoring
/// starts.
///
/// @param n_parts           Number of guarantee-side parts the oracle addresses
/// @param subset_realizable Realizability of a subset of those parts, as for
///                          @ref status_score_mrs
/// @return                  A permutation of [0, n_parts)
std::vector<std::size_t> conflict_degree_order(
    std::size_t n_parts, const SubsetRealizability& subset_realizable);

/// Status score of a FRETISH specification. Its components are the
/// per-requirement conjunctions `condition & response`: a requirement is
/// incoherent when its own condition and response cannot hold together, which
/// already covers either half being unsatisfiable alone. Requirements are
/// checked separately rather than conjoined across the specification, since
/// they fire at different times and need not hold simultaneously.
///
/// Under StatusGrading::Mrs the guarantee-side parts are the guarantees
/// themselves, one part each. They are not split further, as the TLSF path
/// splits its sections into conjuncts: a FRETISH guarantee is a Requirement
/// rather than a formula, so splitting one would produce a part no
/// Specification can hold and no realizability query can be built from through
/// RealizabilityChecker::check_realizability. The corpus carries 3 to 5
/// guarantees per specification, so the scale still has 4 to 6 levels against
/// the tiered scale's 3.
///
/// @p slot_order addresses guarantees by their index in `m_guarantees` rather
/// than by their position in the walk, which runs over the live guarantees
/// alone. A slot is a stable identity -- a removed requirement keeps its place
/// and is flagged -- so an order computed once on the input specification still
/// names the same guarantees after mutation has removed or restored some of
/// them. Empty means index order. This mapping is untested against a campaign;
/// the measurements behind Config::mrs_admission_order are TLSF-path only.
///
/// Under StatusGrading::Aurus neither decomposition is used: the ladder asks
/// about a whole side at a time, so the components are the conjunction of the
/// live assumptions and the conjunction of the live guarantees, each `true`
/// when its side is empty.
///
/// @param specification A specification whose requirements all have m_ltl set
/// @param sat           Satisfiability checker (black)
/// @param real          Realizability checker (ltlsynt)
/// @param grading       Which scale to score on (see StatusGrading)
/// @param slot_order    Guarantee slots in admission order; empty means index
///                      order. Read only under StatusGrading::Mrs
/// @param component_check Whether to test the components here, or take that
///                      tier as decided elsewhere; see ComponentCheck
double specification_status(
    const Specification& specification, SatisfiabilityChecker& sat,
    RealizabilityChecker& real, StatusGrading grading = StatusGrading::Tiered,
    const std::vector<std::size_t>& slot_order = {},
    ComponentCheck component_check = ComponentCheck::Included);

/// The components @ref specification_status tests individually: one
/// `condition & response` conjunction per live requirement, assumptions before
/// guarantees. Tombstoned requirements are skipped, being no longer part of the
/// specification.
///
/// Exposed because each component is one independent `black` call, so a scoring
/// pool can run them concurrently rather than leaving them to the sequential
/// `all_of` inside the score. The score still tests them itself: they guard a
/// walk that costs a synthesis query per guarantee, and hoisting the guard out
/// would spend those queries on candidates it exists to spare. Running them
/// alongside instead leaves the guard reading a warm satisfiability cache.
std::vector<std::string> specification_status_components(
    const Specification& specification);
