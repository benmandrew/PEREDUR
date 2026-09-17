#pragma once

// The running maximal antichain over a sequence of specifications in discovery
// order, and the event log it emits. Shared by both front ends, as the batch
// sweep in filter/antichain.hpp is.
//
// The batch sweep answers one set. A maximality curve asks the same question
// of every prefix of a run's accumulated candidates, and one sweep per time cut
// re-decides pairs the previous cut had already decided. This walk compares
// each arrival against the running antichain alone, which is
// O(n * |antichain|) rather than O(n * |prefix|), and every prefix's answer
// replays from its one log.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "filter/antichain.hpp"
#include "filter/implication.hpp"
#include "fingerprint/lasso.hpp"
#include "fingerprint/prefilter.hpp"
#include "thread_pool.hpp"

namespace antichain {

// One specification and when the run that produced it found it.
template <typename Spec>
struct Arrival {
    std::string m_name;
    double m_elapsed_s{0.0};
    Spec m_specification;
};

// What happened to one specification at one instant. `Admit` and `Drop` are
// the two verdicts on an arrival. `Remove` is what an admission does to a
// member the arrival dominates, and carries the arrival's own timestamp, so the
// log is ordered by `m_elapsed_s` and then by position.
enum class AntichainEvent : std::uint8_t { Admit, Drop, Remove };

struct AntichainRow {
    double m_elapsed_s{0.0};
    std::string m_name;
    AntichainEvent m_event{AntichainEvent::Admit};
    // The antichain's size after this event, so a reader needs no state of its
    // own to plot the curve.
    std::size_t m_size{0};
};

// What the walk cost, for the report and for judging the prefilter.
struct AntichainStats {
    // Ordered implication questions that reached the solver.
    std::size_t m_solver_queries{0};
    // Ordered questions a sampled word answered instead.
    std::size_t m_refuted_directions{0};
    // Scans that stopped at a dominating member rather than walking the whole
    // antichain, which is the saving the batch sweep cannot have.
    std::size_t m_short_circuited{0};
    // Comparisons among the survivors of one wave: the price of scanning
    // concurrently rather than one arrival at a time.
    std::size_t m_reconciled_pairs{0};
};

using AntichainRowCallback = std::function<void(const AntichainRow&)>;

namespace detail {

constexpr std::size_t k_none = static_cast<std::size_t>(-1);

// `m_forward` is "a implies b", which under this order makes a the survivor:
// the direction `merge_subsumed` reads, so the two sweeps agree.
struct Verdict {
    bool m_forward{false};
    bool m_backward{false};
};

enum class Relation : std::uint8_t {
    Incomparable,
    CandidateDropped,
    MemberRemoved,
    Equivalent,
};

// Read with the candidate as side a and the member as side b. Equivalence is
// returned rather than resolved so the caller counts each collapse once, where
// it applies it.
inline Relation relation_of(const Verdict& verdict) {
    if (verdict.m_forward && verdict.m_backward) {
        return Relation::Equivalent;
    }
    if (verdict.m_forward) {
        return Relation::MemberRemoved;
    }
    if (verdict.m_backward) {
        return Relation::CandidateDropped;
    }
    return Relation::Incomparable;
}

// Positions in the wave's snapshot, not arrival indices: the snapshot is what
// the scan saw, and the replay re-reads it against the antichain as it stands.
struct ScanResult {
    std::size_t m_dropped_by{k_none};
    std::vector<std::size_t> m_removes;
};

struct Counters {
    std::atomic<std::size_t> m_solver_queries{0};
    std::atomic<std::size_t> m_refuted{0};
    std::atomic<std::size_t> m_short_circuited{0};
    std::atomic<std::size_t> m_reconciled{0};
};

// One walk over the arrivals, in three steps a wave.
//
// The scan checks every arrival of the wave against a snapshot of the
// antichain taken before the wave, each on its own worker. A stale snapshot is
// enough because both kinds of decision survive whatever the rest of the wave
// does: a candidate dominated by `a` stays dominated when `a` is removed, by
// whatever removed `a`; and a member removed by `x` stays dominated when `x` is
// itself dropped, by whatever dropped `x`. Implication is transitive, so
// neither verdict can be undone.
//
// What a snapshot does not cover is two arrivals of the same wave comparable
// to each other. The reconciliation settles exactly those, as one parallel
// region over the wave's survivors.
//
// The replay then applies the wave in discovery order over the exact
// antichain, from the two regions' answers and with no solver call. That is
// what makes the log a function of the arrivals rather than of the wave size.
template <typename Spec, typename Implies>
class Walk {
   public:
    Walk(const std::vector<Arrival<Spec>>& arrivals,
         std::vector<fingerprint::PackedFingerprint> prints,
         const Implies& implies, const AntichainRowCallback& on_row)
        : m_arrivals(arrivals),
          m_prints(std::move(prints)),
          m_implies(implies),
          m_on_row(on_row),
          m_present(arrivals.size(), 0) {
        m_rows.reserve(arrivals.size());
    }

    void run_wave(std::size_t begin, std::size_t end) {
        m_begin = begin;
        m_snapshot = m_antichain;
        scan_wave(end - begin);
        reconcile();
        replay(end - begin);
    }

    [[nodiscard]] std::vector<AntichainRow> take_rows() {
        return std::move(m_rows);
    }

    [[nodiscard]] const Counters& counters() const { return m_counters; }

   private:
    [[nodiscard]] const Spec& spec(std::size_t index) const {
        return m_arrivals[index].m_specification;
    }

    // A refuted direction is settled as false with no solver call, which can
    // only replace an undecided verdict with the answer it was already given:
    // @p m_implies reads a timeout as false.
    Verdict compare(std::size_t left, std::size_t right) {
        const bool left_refuted = refutes(m_prints, left, right);
        const bool right_refuted = refutes(m_prints, right, left);
        m_counters.m_refuted.fetch_add(
            static_cast<std::size_t>(left_refuted) +
                static_cast<std::size_t>(right_refuted),
            std::memory_order_relaxed);
        Verdict verdict;
        if (!left_refuted) {
            m_counters.m_solver_queries.fetch_add(1, std::memory_order_relaxed);
            verdict.m_forward = m_implies(spec(left), spec(right));
        }
        if (!right_refuted) {
            m_counters.m_solver_queries.fetch_add(1, std::memory_order_relaxed);
            verdict.m_backward = m_implies(spec(right), spec(left));
        }
        return verdict;
    }

    // No similarity key ranks the class, so `prefer_first` falls through to
    // `operator<`, exactly as the batch sweep does when it is given none.
    Relation settle(Relation relation, std::size_t candidate,
                    std::size_t member) {
        if (relation != Relation::Equivalent) {
            return relation;
        }
        ImplicationFilterStats::n_equivalent_collapsed.fetch_add(
            1, std::memory_order_relaxed);
        return prefer_first(spec(candidate), spec(member), 0.0, 0.0)
                   ? Relation::MemberRemoved
                   : Relation::CandidateDropped;
    }

    // Stops at the first member that dominates the candidate, which is sound
    // because the snapshot is an antichain: if member `a` dominates the
    // candidate and the candidate dominates another member `a'`, then `a`
    // dominates `a'`, which two members of an antichain cannot. An equivalent
    // candidate relates to every other member exactly as its twin does, and the
    // twin is incomparable to all of them.
    ScanResult scan_against(std::size_t candidate) {
        ScanResult out;
        for (std::size_t pos = 0; pos < m_snapshot.size(); ++pos) {
            const std::size_t member = m_snapshot[pos];
            const Relation relation = settle(
                relation_of(compare(candidate, member)), candidate, member);
            if (relation == Relation::CandidateDropped) {
                out.m_dropped_by = pos;
                if (pos + 1 < m_snapshot.size()) {
                    m_counters.m_short_circuited.fetch_add(
                        1, std::memory_order_relaxed);
                }
                return out;
            }
            if (relation == Relation::MemberRemoved) {
                out.m_removes.push_back(pos);
            }
        }
        return out;
    }

    void scan_wave(std::size_t count) {
        m_scans.assign(count, {});
        run_bounded_async(
            count, dispatch_window(),
            [this](std::size_t offset) {
                return [this, offset] {
                    m_scans[offset] = scan_against(m_begin + offset);
                };
            },
            [](std::size_t) {});
        m_survivors.clear();
        m_survivor_position.assign(count, k_none);
        for (std::size_t offset = 0; offset < count; ++offset) {
            if (m_scans[offset].m_dropped_by == k_none) {
                m_survivor_position[offset] = m_survivors.size();
                m_survivors.push_back(offset);
            }
        }
    }

    // Eager, losing the scan's short circuit, which is bounded by the wave size
    // squared and mostly answered by the prefilter. Early in a walk the
    // antichain is small, so most of a wave survives and this is the whole
    // cost.
    void reconcile() {
        const std::size_t width = m_survivors.size();
        m_matrix.assign(width * width, {});
        std::vector<std::pair<std::size_t, std::size_t>> pairs;
        pairs.reserve(width * (width > 0 ? width - 1 : 0) / 2);
        for (std::size_t i = 0; i < width; ++i) {
            for (std::size_t j = i + 1; j < width; ++j) {
                pairs.emplace_back(i, j);
            }
        }
        m_counters.m_reconciled.fetch_add(pairs.size(),
                                          std::memory_order_relaxed);
        run_bounded_async(
            pairs.size(), dispatch_window(),
            [this, &pairs, width](std::size_t index) {
                return [this, &pairs, width, index] {
                    const std::size_t left = pairs[index].first;
                    const std::size_t right = pairs[index].second;
                    m_matrix[(left * width) + right] =
                        compare(m_begin + m_survivors[left],
                                m_begin + m_survivors[right]);
                };
            },
            [](std::size_t) {});
    }

    // The matrix holds the lower survivor position as side a, and the replay
    // reaches the earlier survivor first, so a peer that came first is side a
    // and the candidate side b.
    [[nodiscard]] Relation peer_relation(std::size_t offset,
                                         std::size_t peer_offset) {
        const std::size_t width = m_survivors.size();
        const std::size_t self = m_survivor_position[offset];
        const std::size_t peer = m_survivor_position[peer_offset];
        const Verdict stored = peer < self ? m_matrix[(peer * width) + self]
                                           : m_matrix[(self * width) + peer];
        const Verdict oriented =
            peer < self ? Verdict{stored.m_backward, stored.m_forward} : stored;
        return settle(relation_of(oriented), m_begin + offset,
                      m_begin + peer_offset);
    }

    void emit(AntichainRow row) {
        if (m_on_row) {
            m_on_row(row);
        }
        m_rows.push_back(std::move(row));
    }

    void drop(std::size_t offset) {
        const Arrival<Spec>& arrival = m_arrivals[m_begin + offset];
        emit({arrival.m_elapsed_s, arrival.m_name, AntichainEvent::Drop,
              m_antichain.size()});
    }

    void admit(std::size_t offset, const std::vector<std::size_t>& removes) {
        const std::size_t index = m_begin + offset;
        const Arrival<Spec>& arrival = m_arrivals[index];
        for (const std::size_t victim : removes) {
            if (m_present[victim] == 0) {
                continue;
            }
            m_present[victim] = 0;
            m_antichain.erase(
                std::find(m_antichain.begin(), m_antichain.end(), victim));
            emit({arrival.m_elapsed_s, m_arrivals[victim].m_name,
                  AntichainEvent::Remove, m_antichain.size()});
        }
        m_antichain.push_back(index);
        m_present[index] = 1;
        m_admitted.push_back(offset);
        emit({arrival.m_elapsed_s, arrival.m_name, AntichainEvent::Admit,
              m_antichain.size()});
    }

    // A peer that has since been removed is skipped rather than compared: a
    // member still present removed it, and this candidate meets that member in
    // the same loop, so transitivity carries the verdict the peer would give.
    void replay_one(std::size_t offset) {
        if (m_scans[offset].m_dropped_by != k_none) {
            drop(offset);
            return;
        }
        std::vector<std::size_t> removes;
        for (const std::size_t peer_offset : m_admitted) {
            if (m_present[m_begin + peer_offset] == 0) {
                continue;
            }
            const Relation relation = peer_relation(offset, peer_offset);
            if (relation == Relation::CandidateDropped) {
                drop(offset);
                return;
            }
            if (relation == Relation::MemberRemoved) {
                removes.push_back(m_begin + peer_offset);
            }
        }
        for (const std::size_t pos : m_scans[offset].m_removes) {
            removes.push_back(m_snapshot[pos]);
        }
        admit(offset, removes);
    }

    void replay(std::size_t count) {
        m_admitted.clear();
        for (std::size_t offset = 0; offset < count; ++offset) {
            replay_one(offset);
        }
    }

    const std::vector<Arrival<Spec>>& m_arrivals;
    std::vector<fingerprint::PackedFingerprint> m_prints;
    const Implies& m_implies;
    const AntichainRowCallback& m_on_row;
    Counters m_counters;

    std::vector<AntichainRow> m_rows;
    std::vector<std::size_t> m_antichain;
    std::vector<char> m_present;

    std::size_t m_begin{0};
    std::vector<std::size_t> m_snapshot;
    std::vector<ScanResult> m_scans;
    std::vector<std::size_t> m_survivors;
    std::vector<std::size_t> m_survivor_position;
    std::vector<Verdict> m_matrix;
    std::vector<std::size_t> m_admitted;
};

}  // namespace detail

// Walks @p arrivals in the order given, keeping the maximal antichain under
// implication, and returns one row per event.
//
// The order is the caller's: sorted by `m_elapsed_s`, the log is the anytime
// curve; in any other order the final antichain is the same set, maximality
// being a property of the set rather than of the walk.
//
// @p wave_size arrivals are scanned concurrently against a snapshot of the
// antichain taken before the wave, then reconciled against each other. Zero
// means `dispatch_window()`; one makes the walk serial, which is what the tests
// cross the concurrent walk against.
//
// @p implies(a, b) is called concurrently from pool workers and returns
// whether a implies b, reading a timeout as false. @p on_row is called as each
// row is appended, from the serial replay, so a caller may stream the log and
// keep a prefix of it when the walk is killed part-way.
template <typename Spec, typename Implies>
std::vector<AntichainRow> running_antichain(
    const std::vector<Arrival<Spec>>& arrivals, const Implies& implies,
    std::size_t wave_size = 0,
    const std::function<void(std::size_t, std::size_t)>& on_progress = nullptr,
    const AntichainRowCallback& on_row = nullptr,
    AntichainStats* stats = nullptr) {
    if (arrivals.empty()) {
        return {};
    }
    std::vector<Spec> specs;
    specs.reserve(arrivals.size());
    for (const Arrival<Spec>& arrival : arrivals) {
        specs.push_back(arrival.m_specification);
    }
    detail::Walk<Spec, Implies> walk(
        arrivals, fingerprint::prefilter::fingerprints_of(specs), implies,
        on_row);
    const std::size_t wave = wave_size > 0 ? wave_size : dispatch_window();
    for (std::size_t begin = 0; begin < arrivals.size(); begin += wave) {
        const std::size_t end = std::min(begin + wave, arrivals.size());
        walk.run_wave(begin, end);
        if (on_progress) {
            on_progress(end, arrivals.size());
        }
    }
    const detail::Counters& counters = walk.counters();
    const std::size_t refuted =
        counters.m_refuted.load(std::memory_order_relaxed);
    ImplicationFilterStats::n_fingerprint_refuted.fetch_add(
        refuted, std::memory_order_relaxed);
    if (stats != nullptr) {
        stats->m_solver_queries =
            counters.m_solver_queries.load(std::memory_order_relaxed);
        stats->m_refuted_directions = refuted;
        stats->m_short_circuited =
            counters.m_short_circuited.load(std::memory_order_relaxed);
        stats->m_reconciled_pairs =
            counters.m_reconciled.load(std::memory_order_relaxed);
    }
    return walk.take_rows();
}

// The antichain's members at @p elapsed_s, in admission order, replayed from
// @p rows. A drop and a removal are both permanent -- whatever later dominates
// a dominating member dominates what it dominated, implication being transitive
// -- so membership at an instant is the admissions up to it less the removals
// up to it.
inline std::vector<std::string> members_at(
    const std::vector<AntichainRow>& rows, double elapsed_s) {
    std::vector<std::string> members;
    for (const AntichainRow& row : rows) {
        if (row.m_elapsed_s > elapsed_s) {
            break;
        }
        if (row.m_event == AntichainEvent::Admit) {
            members.push_back(row.m_name);
        } else if (row.m_event == AntichainEvent::Remove) {
            const auto found =
                std::find(members.begin(), members.end(), row.m_name);
            if (found != members.end()) {
                members.erase(found);
            }
        }
    }
    return members;
}

}  // namespace antichain
