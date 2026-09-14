#pragma once

// The implication filter run during the search rather than after it. Each
// accumulated repair is pushed as the accumulator finds it, and a coordinator
// thread merges whatever has queued into the maximal set so far, one
// `antichain::merge_subsumed` call per batch. Its pool tasks go out on the
// background queue, so the search's own regions, on the foreground queue, are
// never kept waiting for a queued implication check, only for one a worker
// already holds. What is left for the end of the run is the last batch.
//
// Nothing is deleted. Every accumulated file stays where the accumulator
// wrote it, and `maximal.tsv` beside them names the ones currently maximal,
// rewritten after each batch.

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.hpp"
#include "filter/antichain.hpp"
#include "fingerprint/lasso.hpp"
#include "runner/black.hpp"
#include "thread_pool.hpp"

// Whether @p cfg streams the implication filter. Streaming is the route
// whenever both its inputs are there: nothing queues without the accumulator,
// and there is nothing to run without the filter.
inline bool implication_streams(const Config& cfg) {
    return cfg.accumulate_repairs && cfg.run_implication_filter;
}

// What the streaming filter asks of a specification, per front end. Every
// solver-backed rule is handed the stream's own checker.
template <typename Spec>
struct MaximalStreamRules {
    // Whether the first implies the second, a timeout reading false, as the
    // batch filter reads one.
    std::function<bool(const Spec&, const Spec&, SatisfiabilityChecker&)>
        implies;
    // Ranks the members of an equivalence class. May be empty.
    std::function<double(const Spec&)> similarity;
    // May be empty, which skips the sampled-word prefilter.
    std::function<std::vector<fingerprint::PackedFingerprint>(
        const std::vector<Spec>&)>
        fingerprints;
};

// How many specifications reached each screen, for the filter report.
struct MaximalStreamCounts {
    std::size_t n_pushed{0};
    std::size_t n_distinct{0};
};

template <typename Spec>
class StreamingMaximalFilter {
   public:
    // @p listing_path is where the names of the maximal members go; empty
    // writes no listing. Resets ImplicationFilterStats, which from here on
    // count this filter's sweep alone.
    StreamingMaximalFilter(const Config& cfg, MaximalStreamRules<Spec> rules,
                           std::string listing_path)
        : m_rules(std::move(rules)), m_listing_path(std::move(listing_path)) {
        assert(m_rules.implies);
        // The batch filters' settings (src/repair/evolution.cpp explains
        // them). Set before the coordinator starts, which is the only other
        // thread to read the checker.
        m_checker.set_timeout(cfg.black_timeout);
        m_checker.set_spot_budget(cfg.black_timeout);
        antichain::reset_stats();
        m_coordinator = std::thread([this] { run(); });
    }

    // A filter destroyed unfinished is being unwound past, so it answers its
    // remaining checks without asking the solver and discards what they say.
    ~StreamingMaximalFilter() {
        m_abandoned.store(true, std::memory_order_relaxed);
        close_and_join();
    }

    StreamingMaximalFilter(const StreamingMaximalFilter&) = delete;
    StreamingMaximalFilter& operator=(const StreamingMaximalFilter&) = delete;
    StreamingMaximalFilter(StreamingMaximalFilter&&) = delete;
    StreamingMaximalFilter& operator=(StreamingMaximalFilter&&) = delete;

    // Queues @p spec for the next batch. @p name is its file under the
    // accumulated directory, or empty for a specification no file holds,
    // which then never appears in the listing. Called from one thread only.
    void push(const Spec& spec, std::string name) {
        assert(!m_finished);
        ++m_counts.n_pushed;
        if (!m_seen.insert(spec).second) {
            return;
        }
        ++m_counts.n_distinct;
        {
            const std::scoped_lock lock(m_mutex);
            m_queue.push_back({spec, std::move(name), m_next_sequence++});
        }
        m_ready.notify_one();
    }

    // Waits for every queued batch to merge and returns the maximal set in
    // the order its members were pushed. Rethrows whatever stopped the
    // coordinator, if anything did.
    std::vector<Spec> finish() {
        assert(!m_finished);
        close_and_join();
        m_finished = true;
        if (m_error) {
            std::rethrow_exception(m_error);
        }
        std::vector<Spec> maximal;
        maximal.reserve(m_members.size());
        for (Member& member : m_members) {
            maximal.push_back(std::move(member.spec));
        }
        return maximal;
    }

    // Complete only once finish() has returned.
    [[nodiscard]] const MaximalStreamCounts& counts() const { return m_counts; }

   private:
    struct Entry {
        Spec spec;
        std::string name;
        std::size_t sequence{0};
    };

    struct Member {
        Spec spec;
        std::string name;
        std::size_t sequence{0};
        double key{0.0};
        fingerprint::PackedFingerprint print;
    };

    void close_and_join() {
        {
            const std::scoped_lock lock(m_mutex);
            m_closing = true;
        }
        m_ready.notify_one();
        if (m_coordinator.joinable()) {
            m_coordinator.join();
        }
    }

    // A pool task must never wait on another pool task, so this runs on a
    // thread of its own rather than as a task: it blocks on the regions it
    // opens, and a worker blocked that way is one the region cannot use.
    void run() {
        for (;;) {
            std::vector<Entry> batch;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ready.wait(lock,
                             [this] { return !m_queue.empty() || m_closing; });
                if (m_queue.empty()) {
                    return;
                }
                batch.swap(m_queue);
            }
            if (m_abandoned.load(std::memory_order_relaxed)) {
                return;
            }
            // After a failure the rest of the queue is drained unread, since
            // finish() is going to throw rather than return a set.
            if (m_error) {
                continue;
            }
            try {
                merge(std::move(batch));
                write_listing();
            } catch (...) {
                m_error = std::current_exception();
            }
        }
    }

    bool abandoned() const {
        return m_abandoned.load(std::memory_order_relaxed);
    }

    void merge(std::vector<Entry> batch) {
        for (const Entry& entry : batch) {
            m_any_named = m_any_named || !entry.name.empty();
        }
        std::vector<Spec> incoming;
        incoming.reserve(batch.size());
        for (const Entry& entry : batch) {
            incoming.push_back(entry.spec);
        }
        const std::vector<double> incoming_keys =
            antichain::keys_of(incoming, m_rules.similarity);
        std::vector<fingerprint::PackedFingerprint> incoming_prints;
        if (m_rules.fingerprints) {
            incoming_prints = m_rules.fingerprints(incoming);
        }
        // A batch whose table was lost compares on the solver alone; the
        // members' tables are still good against each other.
        incoming_prints.resize(incoming.size());

        const std::size_t n_settled = m_members.size();
        std::vector<Spec> specs;
        std::vector<double> keys;
        std::vector<fingerprint::PackedFingerprint> prints;
        specs.reserve(n_settled + incoming.size());
        keys.reserve(n_settled + incoming.size());
        prints.reserve(n_settled + incoming.size());
        for (const Member& member : m_members) {
            specs.push_back(member.spec);
            keys.push_back(member.key);
            prints.push_back(member.print);
        }
        specs.insert(specs.end(), incoming.begin(), incoming.end());
        keys.insert(keys.end(), incoming_keys.begin(), incoming_keys.end());
        prints.insert(prints.end(), incoming_prints.begin(),
                      incoming_prints.end());

        const std::vector<std::uint8_t> subsumed = antichain::merge_subsumed(
            specs, keys, prints, n_settled,
            [this](const Spec& lhs, const Spec& rhs) {
                return !abandoned() && m_rules.implies(lhs, rhs, m_checker);
            },
            TaskPriority::Background, {});

        // Members first and then the batch, each in push order, and every
        // batch was pushed after every member: the result stays in push
        // order with no sort.
        std::vector<Member> members;
        for (std::size_t idx = 0; idx < n_settled; ++idx) {
            if (subsumed[idx] == 0U) {
                members.push_back(std::move(m_members[idx]));
            }
        }
        for (std::size_t idx = 0; idx < batch.size(); ++idx) {
            if (subsumed[n_settled + idx] == 0U) {
                assert(members.empty() ||
                       members.back().sequence < batch[idx].sequence);
                members.push_back({std::move(batch[idx].spec),
                                   std::move(batch[idx].name),
                                   batch[idx].sequence, incoming_keys[idx],
                                   std::move(incoming_prints[idx])});
            }
        }
        m_members = std::move(members);
    }

    // Written whole to a temporary file and renamed over the last listing,
    // so a run killed mid-write leaves the previous batch's listing intact
    // rather than half of this one. Nothing is written until a named
    // specification has arrived, which is also what guarantees the
    // accumulator has created the directory.
    void write_listing() {
        if (m_listing_path.empty() || m_listing_failed || !m_any_named) {
            return;
        }
        const std::string temporary = m_listing_path + ".tmp";
        std::ofstream listing(temporary, std::ios::out | std::ios::trunc);
        if (!listing) {
            warn_listing("could not open " + temporary);
            return;
        }
        listing << "file\n";
        for (const Member& member : m_members) {
            if (!member.name.empty()) {
                listing << member.name << "\n";
            }
        }
        listing.close();
        if (!listing) {
            warn_listing("could not write " + temporary);
            return;
        }
        std::error_code error;
        std::filesystem::rename(temporary, m_listing_path, error);
        if (error) {
            warn_listing("could not rename " + temporary + ": " +
                         error.message());
        }
    }

    // Reports once and then stops trying, as the accumulator's own writer
    // does: the listing is a convenience and must never take the run with it.
    void warn_listing(const std::string& message) {
        m_listing_failed = true;
        std::cerr << "warning: " << message
                  << "; the run continues without a maximal listing\n";
    }

    SatisfiabilityChecker m_checker;
    MaximalStreamRules<Spec> m_rules;
    std::string m_listing_path;

    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::vector<Entry> m_queue;
    bool m_closing{false};
    std::atomic<bool> m_abandoned{false};

    // The pushing thread's alone.
    std::unordered_set<Spec> m_seen;
    std::size_t m_next_sequence{0};
    bool m_finished{false};

    // The coordinator's alone until it is joined.
    std::vector<Member> m_members;
    std::exception_ptr m_error;
    bool m_any_named{false};
    bool m_listing_failed{false};

    // The pushing thread's alone, both fields being counted in push().
    MaximalStreamCounts m_counts;

    // Last, so every member it reads is constructed before it starts.
    std::thread m_coordinator;
};
