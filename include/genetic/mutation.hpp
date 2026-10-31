#pragma once

/// @file mutation.hpp
/// @brief Mutation operators for formulae, timing constraints, requirements,
///        and full specifications used in the genetic algorithm.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"

/// Draws a literal from @p pool: an atom drawn uniformly, negated on a coin
/// flip. Two draws, in that order.
Formula draw_literal(const std::vector<std::string>& pool,
                     const RandomSource& random_source);

/// Mutates a formula according to a propositional GA mutation strategy. Atom
/// names are replaced by atoms drawn from @p atoms; if @p atoms is empty,
/// atom names are left unchanged.
///
/// An atom has three moves: rename, negate, or graft a drawn anchor onto it
/// under a drawn connective, which is what guards a positive literal in one
/// draw. A rename prefers a name the atom does not already have. With @p atoms
/// empty there is no anchor to draw, so the graft is dropped.
///
/// @param formula        The formula to mutate
/// @param atoms          Pool of atom names to draw replacements from
/// @param random_source  Random source for branch and selector choices
/// @return               A mutated formula
Formula mutate_formula(const Formula& formula,
                       const std::vector<std::string>& atoms,
                       const RandomSource& random_source);

/// Direction of a mutation within the logical partial order on requirements:
/// Weaken admits more behaviours, Strengthen admits fewer.
enum class Direction : std::uint8_t { Weaken, Strengthen };

/// Mutates a timing constraint by taking a single step through the timing
/// partial order in the direction @p direction.
///
/// Always has no strengthening and Eventually no weakening: each is already the
/// extreme of the order in that direction.
///
/// In the other direction each moves, but only into a timing donated by
/// @p timing_pool, never one conjured from nothing. Both take the same donated
/// set, from opposite sides: each quantified donor contributes `for n ticks`
/// for its own tick count, and a donated Immediately or NextTimepoint
/// contributes itself. With no usable donor the extreme is returned unchanged,
/// so a specification with no interior timing anywhere cannot acquire one.
/// Timings are the same kind of resource as atom names in mutate_formula: drawn
/// from the specification, not invented.
///
/// The steps above are those of the Global order, and @p scope decides what
/// they mean (timing_implies). Under Global and After every step moves in
/// @p direction. The only scopes reverse the order, so the opposite step is
/// taken. A step that is not an edge of @p scope's order returns @p timing
/// unchanged, at the same cost in draws: under In, NotIn and Before a step
/// into Eventually from `next` or `within`, and under the only scopes a step
/// into Always from `until s`, or from `for n` under OnlyIn and OnlyAfter.
///
/// @param timing        The timing value to mutate
/// @param direction     Whether to weaken or strengthen the timing
/// @param scope         The scope of the requirement carrying @p timing
/// @param timing_pool   Timings occurring in the specification, donating the
///                      timings a weakened Always or strengthened Eventually
///                      may take
/// @param random_source Random source for branch and selector choices
/// @return              A mutated timing value
Timing mutate_timing(const Timing& timing, Direction direction, ScopeKind scope,
                     const std::vector<Timing>& timing_pool,
                     const RandomSource& random_source);

/// Collects the distinct timings occurring in @p specification, for use as the
/// @p timing_pool argument of mutate_timing.
std::vector<Timing> collect_timing_pool(const Specification& specification);

/// Mutates a requirement. Each of trigger, response, timing, condition type and
/// scope is mutated independently with probabilities from @p cfg. Response
/// atoms are drawn from @p atoms; trigger (condition) atoms are drawn only from
/// @p condition_atoms, so that output atoms never leak into a trigger.
///
/// The condition-type and scope arms move along an implication order, as the
/// timing arm does, and both are off by default. Each reads its probability
/// before touching @p random_source, so at 0 neither costs a draw and the
/// breeding stream is byte-identical to the one before they existed.
///
/// @param requirement     The requirement to mutate
/// @param atoms           Pool of atom names for response mutation
/// @param condition_atoms Pool of atom names for trigger mutation (inputs only)
/// @param direction       Direction applied to the timing, condition type and
///                        scope fields
/// @param timing_pool     Timings donating tick counts to timing mutation
/// @param mode_pool       Declared modes a scope may move onto. Empty leaves
///                        `Global` as the only reachable scope.
/// @param random_source   Random source for branch and selector choices
/// @param cfg             Configuration providing mutation probabilities
/// @return                A mutated requirement
Requirement mutate_requirement(const Requirement& requirement,
                               const std::vector<std::string>& atoms,
                               const std::vector<std::string>& condition_atoms,
                               Direction direction,
                               const std::vector<Timing>& timing_pool,
                               const std::vector<std::string>& mode_pool,
                               const RandomSource& random_source,
                               const Config& cfg);

/// What breeding knows about the parent an offspring descends from, handed to
/// mutate_specification so that it can aim its move. Built per offspring by
/// breed_offspring from the parent's Scored entry, on the FRETISH path only.
///
/// After a crossover the offspring is no longer the parent it was copied from,
/// so every field but @c original describes the first parent and is only an
/// estimate of the offspring.
struct ParentContext {
    /// The parent's status objective, or nothing when the run scores none.
    std::optional<double> status;
    /// The status reached k_status_realizable: realizable and, under MRS
    /// grading, well-separated.
    bool realizable = false;
    /// The guarantee slots the parent's MRS walk refused (see
    /// ObjectiveRecord::refused). Empty when the walk did not grade it.
    std::vector<std::size_t> refused_guarantees;
    /// Crossover fired on this offspring before mutation.
    bool crossed = false;
    /// The specification the run repairs, which every slot descends from.
    /// Borrowed for the generation, never copied; null when the caller has
    /// none to give.
    const Specification* original = nullptr;
};

/// How often mutate_specification drew its slot from a targeted list under
/// Config::blame_targeted_slots, over a run. Reported under `--diagnostics`
/// and in run.json.
struct SlotTargetingStats {
    /// Rewrites whose slot was drawn from a list narrower than every live
    /// weakenable slot.
    inline static std::atomic<std::size_t> n_targeted{0};
};

/// What Config::direction_gated_mutation did over a run. Reported under
/// `--diagnostics` and in run.json.
struct DirectionGateStats {
    /// Free rewrites proved to move the forbidden way, each returned as the
    /// parent: stronger than an unrealizable parent, or weaker than a
    /// realizable one.
    inline static std::atomic<std::size_t> n_forbidden{0};
    /// Free rewrites incomparable with the parent that the Config::p_sideways
    /// draw refused, each returned as the parent.
    inline static std::atomic<std::size_t> n_sideways_refused{0};
    /// Mutations of a realizable parent that took a step from steps_towards.
    inline static std::atomic<std::size_t> n_steps{0};
};

/// The slots Config::blame_targeted_slots narrows a rewrite to, as global
/// indices (assumptions first, then guarantees) drawn from @p weakenable, in
/// its order. Empty means no narrowing: no context, no status, a crossover
/// offspring, or a list that would be empty.
///
/// An unrealizable parent keeps the refused guarantees and every assumption,
/// since a guarantee outside every unrealizable core cannot make the
/// specification realizable by weakening, while any assumption can. A
/// realizable parent keeps the slots that differ from @p parent's original,
/// counting an appended assumption as differing, since only those can step
/// back towards it.
///
/// @param specification The specification about to be mutated
/// @param weakenable    Its live weakenable slots, as mutate_specification
///                      collects them
/// @param parent        What breeding knows about the parent
std::vector<std::size_t> targeted_mutation_slots(
    const Specification& specification,
    const std::vector<std::size_t>& weakenable, const ParentContext& parent);

/// Mutates a specification by picking one requirement at random and replacing
/// it with a mutated version. The pool of atom names is taken from the
/// specification's in_atoms and out_atoms, on both sides.
///
/// Under Config::blame_targeted_slots the requirement is drawn from
/// targeted_mutation_slots where that is not empty. The draw is the same single
/// `next_index` over a shorter list, so the arm adds no draw, though it changes
/// which slot the draw lands on.
///
/// Under Config::direction_gated_mutation, and only where @p parent carries a
/// status and crossover did not fire, the move follows the parent's status. An
/// unrealizable parent moves as without the gate. A realizable one skips the
/// two structural actions without drawing for them and computes
/// steps_towards its original; one `next_index` then picks among those steps
/// and the slots to rewrite, each counted once, and a rewrite runs with every
/// directed arm at 0, so it is free. A free rewrite is sorted by
/// classify_move: proved stronger than an unrealizable parent or weaker than a
/// realizable one, the parent is returned; incomparable, it is kept with
/// probability Config::p_sideways. Off, or without a status, the draw stream
/// is the one before the gate existed.
///
/// @param specification The specification to mutate (must be non-empty)
/// @param random_source Random source for index and mutation choices
/// @param cfg           Configuration providing mutation probabilities
/// @param parent        What breeding knows about the parent; null aims
///                      nothing
/// @return              A specification with one requirement mutated
/// @throws std::invalid_argument if specification is empty or random_source is
///         not callable
Specification mutate_specification(const Specification& specification,
                                   const RandomSource& random_source,
                                   const Config& cfg,
                                   const ParentContext* parent = nullptr);
