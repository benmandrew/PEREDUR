#pragma once

/// @file diversity.hpp
/// @brief Mechanisms that spread a FRETISH search over unlike repairs: the
///        slot distance between specifications behind
///        `genetic.crowding = "specification"`, and the counters each
///        mechanism writes to run.json.

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.hpp"
#include "genetic/ordered_language.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "hash_combine.hpp"
#include "requirement.hpp"

/// The share of slots in which @p lhs and @p rhs hold different requirements:
/// a Hamming distance over the slots, in [0, 1].
///
/// Slots pair by index, assumptions with assumptions and guarantees with
/// guarantees, as every comparison against the original does. A slot one side
/// lacks (an appended assumption) differs, and so does a slot tombstoned on
/// one side only, since Requirement equality reads `m_removed`. The count is
/// divided by the longer assumption list plus the longer guarantee list; two
/// specifications with no slot at all are at distance 0. Draws nothing.
double specification_distance(const Specification& lhs,
                              const Specification& rhs);

/// What `genetic.crowding = "specification"` did over a run. Reported under
/// `--diagnostics` and in run.json; zero whenever the key is "objective".
struct SpecificationCrowdingStats {
    /// NSGA-II sorts whose crowding was farthest-point order, at parent
    /// ordering and at survivor selection.
    inline static std::atomic<std::size_t> n_sorts{0};
    /// Survivor selections whose truncation cut through a front, so that the
    /// farthest-point order decided which members of that front survived.
    inline static std::atomic<std::size_t> n_cut_fronts{0};
    /// Distances between two specifications computed for those sorts.
    inline static std::atomic<std::size_t> n_distances{0};
};

/// What `genetic.novelty` did over a run. Reported under `--diagnostics` and
/// in run.json; zero whenever the key is off.
struct NoveltyStats {
    /// Novelty values read for a population member at a sort.
    inline static std::atomic<std::size_t> n_evaluations{0};
    /// Pairs of an accumulated repair and a candidate the rule was asked
    /// about. A candidate already measured asks only about the repairs
    /// accumulated since.
    inline static std::atomic<std::size_t> n_checks{0};
    /// Of those, the pairs the rule proved: the repair implies the candidate.
    inline static std::atomic<std::size_t> n_implied{0};
    /// Evaluations of a realizable candidate that some repair implied.
    inline static std::atomic<std::size_t> n_covered{0};
};

/// How many members of the accumulator M edit each slot of the original:
/// differ from the original's requirement there, a tombstone included, or
/// hold an assumption appended past the original's assumptions. Indexed by
/// slot on each side; a slot past the end of a list has count 0.
struct SlotCoverage {
    std::vector<std::size_t> assumptions;
    std::vector<std::size_t> guarantees;
};

/// The coverage count of global slot @p idx (assumptions first, then
/// guarantees) of a specification with @p n_assumptions assumptions.
std::size_t coverage_of(const SlotCoverage& coverage, std::size_t idx,
                        std::size_t n_assumptions);

/// One slot of a specification, by side and index within the side, as the
/// flip lineage keys its record: global indices shift when an assumption is
/// appended, and a side and local index do not.
struct SlotKey {
    bool guarantee = false;
    std::size_t local = 0;

    friend bool operator<(const SlotKey& lhs, const SlotKey& rhs) {
        return lhs.guarantee != rhs.guarantee ? !lhs.guarantee
                                              : lhs.local < rhs.local;
    }
};

/// What a lineage has seen of one slot under `mutation.flip_tabu`.
struct SlotFlips {
    /// An edit of the slot turned an unrealizable parent into a realizable
    /// offspring.
    bool weakened = false;
    /// An edit of the slot turned a realizable parent into an unrealizable
    /// offspring.
    bool strengthened = false;
    /// The slot is tabu while the generation is below this.
    std::size_t tabu_until = 0;
};

/// A lineage's flips per slot, carried from parent to offspring.
using Lineage = std::map<SlotKey, SlotFlips>;

/// What `genetic.novelty_tiebreak` did over a run. Reported under
/// `--diagnostics` and in run.json; zero whenever the key is off. The rule
/// checks behind its values count in NoveltyStats, as novelty's do.
struct NoveltyTiebreakStats {
    /// Sorts whose fronts were ordered by the novelty count first.
    inline static std::atomic<std::size_t> n_sorts{0};
    /// Novelty values read for a population member at those sorts.
    inline static std::atomic<std::size_t> n_evaluations{0};
    /// Of the sorts, those whose order differs from crowding alone.
    inline static std::atomic<std::size_t> n_reordered{0};
    /// Survivor selections that kept another set than crowding alone would.
    inline static std::atomic<std::size_t> n_survivors_changed{0};
};

/// The run-scoped state the diversity mechanisms keep across generations,
/// over specifications of type @p Spec read through OrderedLanguage. One per
/// run, owned by the generation loop and lent to each generation; nothing in
/// it draws from the RandomSource.
template <typename Spec>
class BasicDiversityState {
    using Language = OrderedLanguage<Spec>;
    // C++17 needs the typename that C++20 makes optional here.
    // NOLINTNEXTLINE(readability-redundant-typename)
    using Req = typename Language::Requirement;

   public:
    /// @p accumulated is the run's repair accumulator M, read in its
    /// first-seen order. It only ever grows, which is what lets a measured
    /// candidate catch up on the new members alone. May be null, for a caller
    /// with no accumulator: M is then empty.
    explicit BasicDiversityState(const std::vector<Spec>* accumulated);

    /// The number of members of M that spec_implies_by_rule proves imply
    /// @p candidate: the repairs already found that are at least as strong.
    /// Cached per specification and per requirement pair, so a candidate is
    /// tested against each member of M once.
    std::size_t implied_by(const Spec& candidate);

    /// The slot coverage of M against @p original, brought up to date with
    /// the members accumulated since the last call. @p original must be the
    /// same specification on every call.
    const SlotCoverage& slot_coverage(const Spec& original);

    /// Starts a generation, which the flip lineage counts its tabu spans in.
    void begin_generation() { ++m_generation; }

    /// The global slots (assumptions first, then guarantees) of @p parent
    /// that its lineage holds tabu this generation, ascending.
    [[nodiscard]] std::vector<std::size_t> flipped_slots(
        const Spec& parent) const;

    /// Holds the offspring @p bred of @p parent, realizable or not as
    /// @p parent_realizable says, until the generation is scored. @p edited
    /// is the offspring before simplification, which is compared slot by slot
    /// with @p parent to find the slots the mutation edited. Does nothing
    /// where it edited none.
    void note_offspring(const Spec& parent, bool parent_realizable,
                        const Spec& edited, const Spec& bred);

    /// Gives each held offspring that @p scored holds its lineage: its
    /// parent's, with a flip recorded on each edited slot where the
    /// offspring's realizability, read at @p status_index, differs from the
    /// parent's. A slot flipped both ways goes tabu for @p span generations
    /// and starts afresh. An offspring that already has a lineage keeps it,
    /// and the first offspring held for a specification wins. Then forgets
    /// what it held.
    void settle_offspring(const std::vector<Scored<Spec>>& scored,
                          std::size_t status_index, std::size_t span);

    /// Forgets what is cached about every specification not in @p keep, and
    /// the lineage of every one, which bounds both by the population.
    void retain(const std::vector<Scored<Spec>>& keep);

   private:
    struct Novelty {
        std::size_t checked = 0;
        std::size_t count = 0;
    };
    struct PairHash {
        std::size_t operator()(
            const std::pair<std::string, std::string>& pair) const noexcept {
            const std::hash<std::string> hash;
            return hash_combine(hash(pair.first), hash(pair.second));
        }
    };

    bool memoised_implies(const Req& stronger, const Req& weaker);

    const std::vector<Spec>* m_accumulated;
    /// live_lowerings of each member of M's guarantees, in M's order.
    std::vector<std::unordered_set<std::string>> m_guarantee_lowerings;
    std::unordered_map<Spec, Novelty> m_novelty;
    std::unordered_map<std::pair<std::string, std::string>, bool, PairHash>
        m_pair_memo;
    struct HeldOffspring {
        Spec bred;
        Lineage lineage;
        std::vector<SlotKey> edited;
        bool parent_realizable = false;
    };

    std::size_t m_generation = 0;
    std::unordered_map<Spec, Lineage> m_lineage;
    std::vector<HeldOffspring> m_held;
    SlotCoverage m_coverage;
    /// Members of M already counted into m_coverage.
    std::size_t m_covered = 0;
};

/// The FRETISH diversity state.
using DiversityState = BasicDiversityState<Specification>;

/// The extra objective `genetic.novelty` ranks by, one per element of
/// @p population: minus the number of members of M implying the element where
/// its status, at @p status_index, reads realizable, and 0 otherwise. No
/// realizable repair implies an unrealizable candidate, since realizability is
/// closed under weakening. All 0 where @p status_index is nothing.
template <typename Spec>
std::vector<double> novelty_objective(
    BasicDiversityState<Spec>& state,
    const std::vector<Scored<Spec>>& population,
    std::optional<std::size_t> status_index);

/// What `genetic.spread_init` did to a run's seed population. Reported under
/// `--diagnostics` and in run.json; zero whenever the key is off.
struct SpreadInitStats {
    /// Slots the weakenings were spread over.
    inline static std::size_t n_targets{0};
    /// Seed individuals that are a proved weakening of the original.
    inline static std::size_t n_seeded{0};
    /// Seed individuals left as the original, their slot's draws having
    /// found no weakening the rule proves.
    inline static std::size_t n_failed{0};
};

/// The unscored seed population `genetic.spread_init` asks for, of
/// @p population_size individuals: @p original, then one-step weakenings of
/// it. Individual k (from 1) weakens target slot (k - 1) mod T, the targets
/// being the guarantees in @p refused (local guarantee indices, from the
/// original's MRS walk) that are live and weakenable, or every live
/// weakenable guarantee where none is, followed by every live weakenable
/// assumption.
///
/// A weakening is one mutate_requirement of the slot in the weakening
/// direction (weaker guarantee, stronger assumption), with the propositional
/// rewrites monotone (`p_monotone` at 1), kept only where classify_move
/// proves the result Weaker than @p original. A slot gets up to a few draws;
/// where none is proved, the individual is @p original. Draws from
/// @p random_source.
std::vector<Specification> spread_initial_population(
    const Specification& original, const std::vector<std::size_t>& refused,
    std::size_t population_size, const Config& cfg,
    const RandomSource& random_source);
