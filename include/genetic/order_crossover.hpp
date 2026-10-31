#pragma once

/// @file order_crossover.hpp
/// @brief The order-aware FRETISH crossover: the join of two realizable
///        parents, the meet of two unrealizable ones, and path relinking
///        between one of each.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "config.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"

/// What is proved, without a solver, about a parent's realizability.
enum class ParentVerdict : std::uint8_t { Realizable, Unrealizable, Unknown };

/// The three order crossovers.
enum class OrderCrossoverKind : std::uint8_t { Join, Meet, Relink };

inline constexpr std::size_t k_order_crossover_kinds = 3;

/// The crossover a pair of verdicts calls for, or nothing where one is
/// Unknown. Realizability is closed under weakening, so a join of two
/// realizable parents and a meet of two unrealizable ones are the only bounds
/// whose verdict is open; each of the other four cells is decided in advance
/// and teaches the search nothing. A mixed pair takes path relinking, whose
/// children lie between the realizable parent and the join of the two.
std::optional<OrderCrossoverKind> order_crossover_kind(ParentVerdict first,
                                                       ParentVerdict second);

/// The position of the status objective in a candidate's objective vector
/// under @p cfg, or nothing where the run registers none. Mirrors the
/// registration order of make_fitness_function (src/fitness/factory.hpp):
/// syntactic, then semantic, then status, each only at a positive weight.
std::optional<std::size_t> status_objective_index(const Config& cfg);

/// The verdict on @p parent.
///
/// Realizable when its status objective is k_status_realizable, which both
/// gradings pay only for a realizable candidate. Unrealizable when
/// RealizabilityChecker::proved_unrealizable finds a decided query that
/// proves it. A status below the top does not prove unrealizability: the MRS
/// walk also refuses a part for being ill-separated or undecided. Unknown
/// otherwise.
ParentVerdict parent_verdict(const Scored<Specification>& parent,
                             std::optional<std::size_t> status_index,
                             const RealizabilityChecker& checker);

/// The child of an order crossover. @c m_proved is true when the child
/// carries the operator's implication: a join implying both parents, a meet
/// implied by both, a relinked child implying the realizable parent.
struct OrderCrossoverOutcome {
    std::optional<Specification> m_child;
    bool m_proved = false;
};

/// A specification implying both parents, from specification_join. A slot the
/// rules cannot bound takes one parent's value on a coin, one draw per such
/// slot in slot order, and the child is then a partial join, unproved.
/// Nothing where the slots would merge in the Specification constructor.
OrderCrossoverOutcome join_crossover(const Specification& lhs,
                                     const Specification& rhs,
                                     std::size_t n_original_assumptions,
                                     const RandomSource& random_source);

/// The dual of join_crossover, from specification_meet.
OrderCrossoverOutcome meet_crossover(const Specification& lhs,
                                     const Specification& rhs,
                                     std::size_t n_original_assumptions,
                                     const RandomSource& random_source);

/// Path relinking from @p realizable towards @p unrealizable. A slot where
/// @p unrealizable is proved strictly stronger, as a part of `A -> G`, is a
/// site: a guarantee it implies and is not implied by, a guarantee it keeps
/// live where @p realizable removed it, an original assumption it is implied
/// by and does not imply, or one it removed. The child adopts
/// @p unrealizable's value on a random non-empty subset of the sites: a coin
/// per site, and one site drawn outright when every coin fell tails.
/// Appended assumptions stay @p realizable's.
///
/// The child implies @p realizable and is implied by any join of the two
/// parents, which is unrealizable. Nothing where there is no site.
OrderCrossoverOutcome relink_crossover(const Specification& realizable,
                                       const Specification& unrealizable,
                                       std::size_t n_original_assumptions,
                                       const RandomSource& random_source);

/// Runs the @p kind crossover over @p first and @p second and counts it in
/// OrderCrossoverStats. For Relink, the realizable parent is @p first when
/// @p first_realizable holds. A child equal to either parent is dropped, so
/// the outcome then holds none.
OrderCrossoverOutcome order_crossover(OrderCrossoverKind kind,
                                      const Specification& first,
                                      const Specification& second,
                                      bool first_realizable,
                                      std::size_t n_original_assumptions,
                                      const RandomSource& random_source);

/// What the order crossover did over a run, per kind, in OrderCrossoverKind
/// order. Reported under `--diagnostics` and in run.json.
struct OrderCrossoverStats {
    /// Crossovers the arm ran.
    inline static std::array<std::atomic<std::size_t>, k_order_crossover_kinds>
        n_attempted{};
    /// Of those, the ones whose child differs from both parents and carries
    /// the operator's implication.
    inline static std::array<std::atomic<std::size_t>, k_order_crossover_kinds>
        n_proved{};
    /// Of those attempted, the ones whose offspring, after mutation and
    /// simplification, scored realizable the generation it was bred.
    inline static std::array<std::atomic<std::size_t>, k_order_crossover_kinds>
        n_realizable{};

    /// Holds @p offspring until the generation is scored, where @p kind says
    /// an order crossover made it; does nothing otherwise.
    static void note_offspring(std::optional<OrderCrossoverKind> kind,
                               const Specification& offspring);

    /// Counts each held offspring that @p scored rates realizable, then
    /// forgets them all: an offspring a filter dropped was never scored.
    static void settle(const std::vector<Scored<Specification>>& scored,
                       std::optional<std::size_t> status_index);
};
