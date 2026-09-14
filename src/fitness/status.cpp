#include "fitness/status.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "filter/well_separation.hpp"
#include "requirement.hpp"
#include "thread_pool.hpp"

namespace {

bool all_components_satisfiable(const std::vector<std::string>& components,
                                SatisfiabilityChecker& sat) {
    return std::all_of(
        components.begin(), components.end(),
        [&sat](const std::string& formula) {
            // An unanswered query counts as unsatisfiable,
            // matching the rest of the scoring path: a timeout
            // must not promote a candidate above one that was
            // decided.
            return sat.check_satisfiability(formula).value_or(false);
        });
}

}  // namespace

double status_score(const std::vector<std::string>& components,
                    SatisfiabilityChecker& sat,
                    const std::function<bool()>& is_realizable) {
    if (!all_components_satisfiable(components, sat)) {
        return k_status_component_unsatisfiable;
    }
    return is_realizable() ? k_status_realizable : k_status_unrealizable;
}

double status_score_aurus(const std::string& assumptions,
                          const std::string& guarantees,
                          SatisfiabilityChecker& sat,
                          const std::function<bool()>& is_realizable) {
    // Both asked, and in this order, whatever the first answers: the pair is
    // what separates BOTTOM from the two one-sided tiers, and the order is the
    // one every candidate asks them in, so the cache keys line up across a
    // population that mostly shares its sides.
    const std::optional<bool> assumptions_sat =
        sat.check_satisfiability(assumptions);
    const std::optional<bool> guarantees_sat =
        sat.check_satisfiability(guarantees);
    if (!assumptions_sat.has_value() || !guarantees_sat.has_value()) {
        // AuRUS leaves the status UNKNOWN on a query its solver could not
        // settle, and pays UNKNOWN exactly what it pays BOTTOM.
        return k_status_component_unsatisfiable;
    }
    if (!*assumptions_sat && !*guarantees_sat) {
        return k_status_component_unsatisfiable;
    }
    if (!*assumptions_sat) {
        return k_status_aurus_guarantees_only;
    }
    if (!*guarantees_sat) {
        return k_status_aurus_assumptions_only;
    }
    // Reached only where both sides stand on their own, so the answer is not
    // already known: two consistent halves can still contradict each other.
    const std::optional<bool> jointly_sat = sat.check_satisfiability(
        "(" + assumptions + ") & (" + guarantees + ")");
    if (!jointly_sat.has_value()) {
        return k_status_component_unsatisfiable;
    }
    if (!*jointly_sat) {
        return k_status_aurus_contradictory;
    }
    // No well-separation query behind this one, unlike status_score and
    // status_score_mrs. See the header: AuRUS never asks it, and an arm that
    // did would not be the ladder it is named after.
    return is_realizable() ? k_status_realizable : k_status_unrealizable;
}

std::vector<std::size_t> project_admission_order(
    const std::vector<std::size_t>& reference, std::size_t n_parts) {
    std::vector<std::size_t> order;
    order.reserve(n_parts);
    std::vector<bool> covered(n_parts, false);
    for (const std::size_t part : reference) {
        // A reference may address parts a shorter walk no longer has, and a
        // malformed one may repeat a part; either would corrupt the walk.
        if (part < n_parts && !covered[part]) {
            covered[part] = true;
            order.push_back(part);
        }
    }
    for (std::size_t part = 0; part < n_parts; ++part) {
        if (!covered[part]) {
            order.push_back(part);
        }
    }
    return order;
}

double status_score_mrs(const std::vector<std::string>& components,
                        std::size_t n_parts, SatisfiabilityChecker& sat,
                        const SubsetRealizability& subset_realizable,
                        const std::vector<std::size_t>& admission_order) {
    if (!all_components_satisfiable(components, sat)) {
        return k_status_component_unsatisfiable;
    }
    if (n_parts == 0) {
        return k_status_realizable;
    }
    // Grown once and reused across the walk; the oracle reads it and does not
    // retain it. A rejected part is erased, so `kept` is exactly the accepted
    // set at every step -- which is what makes the queries recur across
    // near-identical candidates and hit RealizabilityChecker's cache.
    //
    // Held sorted rather than in admission sequence, so that a set of parts
    // lowers to one formula string whatever order reached it. Under index order
    // the two coincide, which is why the walk could push and pop while that was
    // the only order it ran.
    std::vector<std::size_t> kept;
    kept.reserve(n_parts);
    for (const std::size_t part :
         project_admission_order(admission_order, n_parts)) {
        const auto slot = std::lower_bound(kept.begin(), kept.end(), part);
        const auto inserted = kept.insert(slot, part);
        if (!subset_realizable(kept)) {
            kept.erase(inserted);
        }
    }
    return static_cast<double>(kept.size()) / static_cast<double>(n_parts);
}

std::vector<std::size_t> conflict_degree_order(
    std::size_t n_parts, const SubsetRealizability& subset_realizable) {
    std::vector<std::size_t> order(n_parts);
    std::iota(order.begin(), order.end(), 0);
    if (n_parts < 3) {
        // Two parts have equal degree by construction and one has none, so no
        // reordering is available and the queries would buy nothing.
        return order;
    }
    const std::size_t in_flight =
        std::max<std::size_t>(1, global_thread_pool().size());

    // A part unrealizable on its own can never be kept, so its conflicts say
    // nothing about where it belongs; it is ranked past every other part
    // instead. This costs n queries the pairwise pass would otherwise repeat,
    // and RealizabilityChecker memoises them for the walk that follows.
    std::vector<bool> solo(n_parts, false);
    run_bounded_async(
        n_parts, in_flight,
        [&subset_realizable](std::size_t part) {
            return [&subset_realizable, part] {
                return subset_realizable(std::vector<std::size_t>{part});
            };
        },
        [&solo](std::size_t part, bool realizable) {
            solo[part] = realizable;
        });

    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    pairs.reserve(n_parts * (n_parts - 1) / 2);
    for (std::size_t i = 0; i < n_parts; ++i) {
        for (std::size_t j = i + 1; j < n_parts; ++j) {
            pairs.emplace_back(i, j);
        }
    }
    std::vector<std::size_t> degree(n_parts, 0);
    run_bounded_async(
        pairs.size(), in_flight,
        [&subset_realizable, &pairs](std::size_t index) {
            // Copied out of the pair rather than bound structurally: C++17
            // forbids capturing a structured binding in a lambda.
            const std::size_t left = pairs[index].first;
            const std::size_t right = pairs[index].second;
            return [&subset_realizable, left, right] {
                return subset_realizable(std::vector<std::size_t>{left, right});
            };
        },
        [&degree, &pairs](std::size_t index, bool realizable) {
            if (realizable) {
                return;
            }
            ++degree[pairs[index].first];
            ++degree[pairs[index].second];
        });

    const auto rank = [&degree, &solo, n_parts](std::size_t part) {
        return solo[part] ? degree[part] : n_parts + 1;
    };
    // Ties broken on the index, which makes the comparator a total order, so
    // the result is a function of the specification alone -- not of how the
    // pairwise queries interleaved, and not of whether the sort is stable.
    // Relying on stability has bitten this repository once already; see
    // crowding_distances.
    std::sort(order.begin(), order.end(),
              [&rank](std::size_t left, std::size_t right) {
                  return std::make_pair(rank(left), left) <
                         std::make_pair(rank(right), right);
              });
    return order;
}

namespace {

// `specification` with only the guarantees at `indices` kept, and its
// assumptions, inputs and outputs unchanged. The environment side is never
// relaxed: weakening an assumption can only make synthesis harder, so it has no
// place in a subset walk looking for what the system can still achieve.
//
// `indices` are positions in the walk, which runs over the live guarantees
// alone; `slots` maps each back to the guarantee vector it came from. Walking
// the raw vector instead would hand a removed guarantee to ltlsynt as a part
// the system has to satisfy.
Specification with_guarantee_subset(const Specification& specification,
                                    const std::vector<std::size_t>& slots,
                                    const std::vector<std::size_t>& indices) {
    Specification subset = specification;
    subset.m_guarantees.clear();
    subset.m_guarantees.reserve(indices.size());
    for (const std::size_t index : indices) {
        assert(index < slots.size());
        subset.m_guarantees.push_back(specification.m_guarantees[slots[index]]);
    }
    return subset;
}

// One side of a FRETISH specification as a single LTL formula, tombstones
// dropped. An empty side is `true` rather than the empty string, which is what
// an absent side means and what black would refuse to parse.
//
// AuRUS's status ladder asks about a whole side at a time, where every other
// scale here asks per requirement, so this conjunction exists only for
// status_score_aurus.
std::string side_conjunction(const std::vector<Requirement>& requirements) {
    std::string conjunction;
    for (const Requirement& req : requirements) {
        if (req.m_removed) {
            continue;
        }
        if (!conjunction.empty()) {
            conjunction += " & ";
        }
        conjunction += "(" + req.m_ltl + ")";
    }
    return conjunction.empty() ? "true" : conjunction;
}

// A walk order over live guarantee *positions*, from a reference order over
// guarantee *slots*.
//
// The FRETISH walk runs over the live guarantees alone, so a position means
// something different once a guarantee is removed or restored, and an order
// stored as positions would drift as mutation toggles them. A slot is stable:
// m_guarantees keeps a removed requirement in place and flags it, so the index
// a guarantee sits at never moves. Slots the reference does not name sort last
// among themselves in walk order.
std::vector<std::size_t> position_order_from_slots(
    const std::vector<std::size_t>& slot_order,
    const std::vector<std::size_t>& slots) {
    if (slot_order.empty()) {
        return {};
    }
    constexpr std::size_t k_unranked = std::numeric_limits<std::size_t>::max();
    std::size_t widest = 0;
    for (const std::size_t slot : slot_order) {
        widest = std::max(widest, slot + 1);
    }
    std::vector<std::size_t> rank(widest, k_unranked);
    for (std::size_t index = 0; index < slot_order.size(); ++index) {
        rank[slot_order[index]] = index;
    }
    const auto rank_of = [&rank, widest](std::size_t slot) {
        return slot < widest ? rank[slot] : k_unranked;
    };
    std::vector<std::size_t> order(slots.size());
    std::iota(order.begin(), order.end(), 0);
    // Total on (rank, position), for the same reason as conflict_degree_order:
    // unranked slots keep walk order among themselves without the sort having
    // to be stable for them to.
    std::sort(order.begin(), order.end(),
              [&rank_of, &slots](std::size_t left, std::size_t right) {
                  return std::make_pair(rank_of(slots[left]), left) <
                         std::make_pair(rank_of(slots[right]), right);
              });
    return order;
}

}  // namespace

std::vector<std::string> specification_status_components(
    const Specification& specification) {
    // Requirements are checked one at a time rather than conjoined across the
    // specification: they fire at different times (different conditions,
    // Trigger vs Continual), so their conditions and responses need not be
    // simultaneously satisfiable. Testing `condition & response` per
    // requirement also subsumes testing either half alone, since an
    // unsatisfiable half makes the conjunction unsatisfiable.
    std::vector<std::string> components;
    components.reserve(specification.m_assumptions.size() +
                       specification.m_guarantees.size());
    const auto add = [&components](const std::vector<Requirement>& reqs) {
        for (const Requirement& req : reqs) {
            // A removed requirement is not a component of the specification;
            // scoring its satisfiability would charge a candidate for content
            // it no longer has.
            if (req.m_removed) {
                continue;
            }
            components.push_back("(" + req.m_condition.to_string() + ") & (" +
                                 req.m_response.to_string() + ")");
        }
    };
    add(specification.m_assumptions);
    add(specification.m_guarantees);
    return components;
}

double specification_status(const Specification& specification,
                            SatisfiabilityChecker& sat,
                            RealizabilityChecker& real, StatusGrading grading,
                            const std::vector<std::size_t>& slot_order,
                            ComponentCheck component_check) {
    if (grading == StatusGrading::Aurus) {
        // Sides rather than requirements, and no component tier: the ladder is
        // AuRUS's whole scale, not a refinement of PEREDUR's. See
        // status_score_aurus.
        return status_score_aurus(
            side_conjunction(specification.m_assumptions),
            side_conjunction(specification.m_guarantees), sat,
            [&specification, &real] {
                // Realizability alone. Well-separation is deliberately not
                // folded in here, where both other scales fold it in; the
                // header says why. The output gate still asks it.
                return count_live(specification.m_guarantees) == 0 ||
                       real.check_realizability(specification).value_or(false);
            });
    }

    // An empty component list passes the tier vacuously, which is exactly what
    // ComponentCheck::Skipped asks for; both scales below already handle it.
    const std::vector<std::string> components =
        component_check == ComponentCheck::Included
            ? specification_status_components(specification)
            : std::vector<std::string>{};

    if (grading == StatusGrading::Mrs) {
        const std::vector<std::size_t> slots =
            live_indices(specification.m_guarantees);
        return status_score_mrs(
            components, slots.size(), sat,
            [&specification, &slots,
             &real](const std::vector<std::size_t>& indices) {
                const Specification subset =
                    with_guarantee_subset(specification, slots, indices);
                // Undecided resolves as unrealizable, so the part is rejected:
                // a timed-out query must not buy a candidate a point.
                return real.check_realizability(subset).value_or(false) &&
                       !specification_is_not_well_separated(subset, real);
            },
            position_order_from_slots(slot_order, slots));
    }

    return status_score(components, sat, [&specification, &real] {
        // No guarantees leaves the implication with a `true` consequent, which
        // is realizable whatever the assumptions say; skip the solver rather
        // than ask it a question with a known answer.
        const bool realizable =
            count_live(specification.m_guarantees) == 0 ||
            real.check_realizability(specification).value_or(false);
        // Second, and only where the first said yes: a candidate that is
        // already unrealizable cannot be realizable for the wrong reason, and
        // the query is a whole ltlsynt call.
        return realizable &&
               !specification_is_not_well_separated(specification, real);
    });
}
