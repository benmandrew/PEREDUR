#include "fitness/status.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "bounded_async.hpp"
#include "filter/well_separation.hpp"
#include "profile.hpp"
#include "requirement.hpp"
#include "runner/process.hpp"
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

// Set once at startup from Config::mrs_lookahead_kill_grace, read by every
// lookahead task, hence atomic.
std::atomic<std::int64_t> g_lookahead_kill_grace_ms{2'000};

// Past this the tree of lookahead sets costs more to enumerate each step than
// a pool's width of workers could ever run.
constexpr std::size_t k_max_speculation_depth = 8;

std::vector<std::size_t> with_part(std::vector<std::size_t> parts,
                                   std::size_t part) {
    parts.insert(std::lower_bound(parts.begin(), parts.end(), part), part);
    return parts;
}

// The lookahead behind status_score_mrs. It holds a node per query it has
// submitted that the walk may still ask. Only the walk moves a node out of
// Pending, by cancelling it, and only a worker moves it on from Running.
class WalkLookahead {
   public:
    WalkLookahead(const SubsetRealizability& oracle,
                  const std::vector<std::size_t>& order, std::size_t depth)
        : m_oracle(oracle),
          m_order(order),
          m_depth(std::min(depth, k_max_speculation_depth)),
          m_shared(std::make_shared<Shared>()) {}

    WalkLookahead(const WalkLookahead&) = delete;
    WalkLookahead& operator=(const WalkLookahead&) = delete;
    WalkLookahead(WalkLookahead&&) = delete;
    WalkLookahead& operator=(WalkLookahead&&) = delete;

    // A started task borrows m_oracle, which the caller owns, so none may
    // outlive the walk. A cancelled one can still sit in the pool's queue, but
    // it holds its own node and m_shared and touches nothing else. The walk has
    // asked its last query, so every node still here is one it will never
    // read, and a running one is killed rather than waited out.
    ~WalkLookahead() {
        for (const auto& entry : m_nodes) {
            if (!cancel(*entry.second)) {
                kill(*entry.second);
            }
        }
        for (const std::shared_ptr<Node>& node : m_abandoned) {
            kill(*node);
        }
        for (const auto& entry : m_nodes) {
            wait_done(*entry.second);
        }
        for (const std::shared_ptr<Node>& node : m_abandoned) {
            wait_done(*node);
        }
    }

    // Called with the walk about to ask `query`, its step's part inserted.
    void before_query(std::size_t step, const std::vector<std::size_t>& query) {
        std::shared_ptr<Node> own;
        if (const auto found = m_nodes.find(query); found != m_nodes.end()) {
            own = found->second;
            m_nodes.erase(found);
            // Not yet started, so the walk asks it inline rather than wait on
            // a task that needs a free worker to run.
            if (cancel(*own)) {
                own.reset();
            }
        }
        refresh(step, query);
        if (own) {
            wait_done(*own);
            if (own->m_answered) {
                MrsSpeculationStats::n_consumed++;
            }
        }
    }

   private:
    enum class State : std::uint8_t { Pending, Running, Done, Cancelled };

    struct Node {
        std::vector<std::size_t> m_set;
        std::atomic<State> m_state{State::Pending};
        // Set by the walk once the node can no longer be read, which kills
        // the task's exec in flight (see ExecCancelScope). Never set on a node
        // the walk is waiting for.
        std::atomic<bool> m_kill{false};
        // Set once the walk settles the node's branch the other way. Its exec
        // is killed only past Config::mrs_lookahead_kill_grace, so a query
        // nearly done still lands in the caches for later candidates.
        std::atomic<bool> m_abandon{false};
        // Written before m_state turns Done, and read only after.
        bool m_answered = false;
    };

    // Shared with the tasks, which signal through it after the walk may have
    // stopped waiting on their node.
    struct Shared {
        std::mutex m_mutex;
        std::condition_variable m_done;
    };

    static bool cancel(Node& node) {
        State expected = State::Pending;
        return node.m_state.compare_exchange_strong(expected, State::Cancelled);
    }

    static void kill(Node& node) {
        node.m_kill.store(true, std::memory_order_release);
    }

    static void abandon(Node& node) {
        node.m_abandon.store(true, std::memory_order_release);
    }

    void wait_done(const Node& node) {
        const State state = node.m_state.load();
        if (state == State::Done || state == State::Cancelled) {
            return;
        }
        PEREDUR_PROFILE_SCOPE("status/mrs-lookahead-wait");
        std::unique_lock<std::mutex> lock(m_shared->m_mutex);
        m_shared->m_done.wait(
            lock, [&node] { return node.m_state.load() == State::Done; });
    }

    // The sets the next m_depth steps can ask under every outcome of the steps
    // before them: nearest step first, and within a step the branch that
    // accepted the earlier part ahead of the one that rejected it.
    [[nodiscard]] std::vector<std::vector<std::size_t>> reachable(
        std::size_t step, const std::vector<std::size_t>& query) const {
        std::vector<std::size_t> before = query;
        before.erase(
            std::lower_bound(before.begin(), before.end(), m_order[step]));
        std::vector<std::vector<std::size_t>> bases{std::move(before)};
        std::vector<std::vector<std::size_t>> sets;
        for (std::size_t ahead = 1;
             ahead <= m_depth && step + ahead < m_order.size(); ++ahead) {
            const std::size_t unsettled = m_order[step + ahead - 1];
            std::vector<std::vector<std::size_t>> grown;
            grown.reserve(2 * bases.size());
            for (std::vector<std::size_t>& base : bases) {
                grown.push_back(with_part(base, unsettled));
                grown.push_back(std::move(base));
            }
            bases = std::move(grown);
            for (const std::vector<std::size_t>& base : bases) {
                sets.push_back(with_part(base, m_order[step + ahead]));
            }
        }
        return sets;
    }

    void refresh(std::size_t step, const std::vector<std::size_t>& query) {
        const std::vector<std::vector<std::size_t>> wanted =
            reachable(step, query);
        const std::set<std::vector<std::size_t>> live(wanted.begin(),
                                                      wanted.end());
        // A set the walk cannot reach now it never can again: the latest step
        // among its parts fixes which step would ask it, and the others fix
        // the outcome of every step before that one.
        for (auto entry = m_nodes.begin(); entry != m_nodes.end();) {
            if (live.count(entry->first) > 0) {
                ++entry;
                continue;
            }
            if (!cancel(*entry->second) &&
                entry->second->m_state.load() != State::Done) {
                abandon(*entry->second);
                m_abandoned.push_back(entry->second);
            }
            entry = m_nodes.erase(entry);
        }
        m_abandoned.erase(std::remove_if(m_abandoned.begin(), m_abandoned.end(),
                                         [](const std::shared_ptr<Node>& node) {
                                             return node->m_state.load() ==
                                                    State::Done;
                                         }),
                          m_abandoned.end());

        std::size_t budget = global_thread_pool().idle_workers();
        for (const std::vector<std::size_t>& set : wanted) {
            if (budget == 0) {
                break;
            }
            if (m_nodes.count(set) == 0) {
                launch(set);
                --budget;
            }
        }
    }

    void launch(const std::vector<std::size_t>& set) {
        auto node = std::make_shared<Node>();
        node->m_set = set;
        m_nodes.emplace(set, node);
        MrsSpeculationStats::n_issued++;
        // Background as well as gated on idle workers: a scoring part queued
        // after this one still runs first.
        global_thread_pool().submit(
            [shared = m_shared, node, oracle = &m_oracle] {
                State expected = State::Pending;
                if (!node->m_state.compare_exchange_strong(expected,
                                                           State::Running)) {
                    return;
                }
                MrsSpeculationStats::n_started++;
                bool answered = false;
                try {
                    // A killed exec raises past the runners' own handlers,
                    // which is what keeps it out of every cache: they memoise
                    // only an answer that came back.
                    const ExecCancelScope scope(
                        node->m_kill, &node->m_abandon,
                        std::chrono::milliseconds{
                            g_lookahead_kill_grace_ms.load()});
                    (void)(*oracle)(node->m_set);
                    answered = true;
                } catch (const ExecCancelled&) {
                    MrsSpeculationStats::n_killed++;
                } catch (...) {
                    // Left for the walk to meet if it asks this query itself;
                    // on a branch it does not take, the error buys nothing.
                    answered = false;
                }
                {
                    const std::scoped_lock lock(shared->m_mutex);
                    node->m_answered = answered;
                    node->m_state.store(State::Done);
                }
                shared->m_done.notify_all();
            },
            TaskPriority::Background);
    }

    const SubsetRealizability& m_oracle;
    const std::vector<std::size_t>& m_order;
    std::size_t m_depth;
    std::shared_ptr<Shared> m_shared;
    std::map<std::vector<std::size_t>, std::shared_ptr<Node>> m_nodes;
    // Started tasks for sets the walk can no longer reach, which it must
    // still outlast.
    std::vector<std::shared_ptr<Node>> m_abandoned;
};

}  // namespace

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
    // No well-separation query behind this one, unlike status_score_mrs. See
    // the header: AuRUS never asks it, and an arm that did would not be the
    // ladder it is named after.
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

void set_mrs_lookahead_kill_grace(std::chrono::milliseconds grace) {
    g_lookahead_kill_grace_ms.store(grace.count());
}

double status_score_mrs(const std::vector<std::string>& components,
                        std::size_t n_parts, SatisfiabilityChecker& sat,
                        const SubsetRealizability& subset_realizable,
                        const std::vector<std::size_t>& admission_order,
                        const std::string& whole,
                        std::size_t speculation_depth) {
    if (!all_components_satisfiable(components, sat)) {
        return k_status_component_unsatisfiable;
    }
    if (n_parts == 0) {
        return k_status_realizable;
    }
    // After the components, which are cheaper and catch most of what this
    // does, and before the walk it exists to spare. An undecided query passes:
    // the whole specification is the query most likely to exhaust the budget,
    // and reading a timeout as unsatisfiable would zero exactly the largest
    // candidates, where the walk can still grade them.
    if (!whole.empty()) {
        MrsScreenStats::n_screened++;
        const std::optional<bool> satisfiable = sat.check_satisfiability(whole);
        if (!satisfiable.has_value()) {
            MrsScreenStats::n_undecided++;
        } else if (!*satisfiable) {
            MrsScreenStats::n_unsatisfiable++;
            return k_status_component_unsatisfiable;
        }
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
    const std::vector<std::size_t> order =
        project_admission_order(admission_order, n_parts);
    // Declared after `order`, which it borrows, so it is destroyed first, and
    // its destructor's wait covers an exception out of the oracle too.
    std::optional<WalkLookahead> lookahead;
    if (speculation_depth > 0) {
        lookahead.emplace(subset_realizable, order, speculation_depth);
    }
    for (std::size_t step = 0; step < order.size(); ++step) {
        const std::size_t part = order[step];
        const auto slot = std::lower_bound(kept.begin(), kept.end(), part);
        const auto inserted = kept.insert(slot, part);
        if (lookahead) {
            lookahead->before_query(step, kept);
        }
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
// AuRUS's status ladder asks about a whole side at a time, and the MRS walk's
// whole-specification screen conjoins the two sides.
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
    // A screen ahead of the realizability walk, not a satisfiability test;
    // the header says why condition and response are tested apart.
    std::vector<std::string> components;
    components.reserve(2 * (specification.m_assumptions.size() +
                            specification.m_guarantees.size()));
    const auto add = [&components](const std::vector<Requirement>& reqs) {
        for (const Requirement& req : reqs) {
            // A removed requirement is not a component of the specification;
            // scoring its satisfiability would charge a candidate for content
            // it no longer has.
            if (req.m_removed) {
                continue;
            }
            components.push_back(req.m_condition.to_string());
            if (!std::holds_alternative<timing::Until>(req.m_timing) &&
                !std::holds_alternative<timing::Before>(req.m_timing)) {
                components.push_back(req.m_response.to_string());
            }
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
                            ComponentCheck component_check,
                            std::size_t speculation_depth) {
    if (grading == StatusGrading::Aurus) {
        // Sides rather than requirements, and no component tier: the ladder is
        // AuRUS's whole scale, not a refinement of PEREDUR's. See
        // status_score_aurus.
        return status_score_aurus(
            side_conjunction(specification.m_assumptions),
            side_conjunction(specification.m_guarantees), sat,
            [&specification, &real] {
                // Realizability alone. Well-separation is deliberately not
                // folded in here, where the MRS walk folds it in; the header
                // says why. The output gate still asks it.
                return count_live(specification.m_guarantees) == 0 ||
                       real.check_realizability(specification).value_or(false);
            });
    }

    // An empty component list passes the tier vacuously, which is exactly what
    // ComponentCheck::Skipped asks for; the walk below already handles it.
    const std::vector<std::string> components =
        component_check == ComponentCheck::Included
            ? specification_status_components(specification)
            : std::vector<std::string>{};

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
        position_order_from_slots(slot_order, slots),
        "(" + side_conjunction(specification.m_assumptions) + ") & (" +
            side_conjunction(specification.m_guarantees) + ")",
        speculation_depth);
}
