#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "filter/running_antichain.hpp"
#include "runner/black.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

using Arrivals = std::vector<antichain::Arrival<tlsf::Specification>>;
using Rows = std::vector<antichain::AntichainRow>;

tlsf::Specification parse_spec(const std::string& main_body) {
    return tlsf::parse("INFO { SEMANTICS: Mealy; }\nMAIN {\n" + main_body +
                       "\n}\n");
}

// Adding an assumption weakens, so `chain(0)` implies `chain(1)` implies
// `chain(2)` and none of them implies back. Under this order the implying side
// is the survivor, so a walk over any permutation of the three keeps `chain(0)`
// alone.
tlsf::Specification chain(std::size_t rung) {
    const std::array<std::string, 3> assumptions = {
        "", "ASSUME { G F a; } ", "ASSUME { G F a; G F b; } "};
    return parse_spec("INPUTS { a; } OUTPUTS { b; } " + assumptions.at(rung) +
                      "GUARANTEE { G (a -> b); }");
}

// Neither implies nor is implied by any rung of the chain.
tlsf::Specification off_axis() {
    return parse_spec(
        "INPUTS { a; } OUTPUTS { b; } GUARANTEE { G (a -> !b); }");
}

// `G (!a | b)` is `G (a -> b)` written another way, so this is equivalent to
// `chain(0)` and exactly one of the two may survive.
tlsf::Specification restated() {
    return parse_spec("INPUTS { a; } OUTPUTS { b; } GUARANTEE { G (!a | b); }");
}

Arrivals arrivals_of(const std::vector<tlsf::Specification>& specs) {
    Arrivals arrivals;
    arrivals.reserve(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        arrivals.push_back({"cand" + std::to_string(i),
                            static_cast<double>(i) + 1.0, specs[i]});
    }
    return arrivals;
}

// Weakest first, so every strengthening that follows has to evict what is
// already in the antichain. The other order would drop every arrival after the
// first on sight and never exercise a removal.
std::vector<tlsf::Specification> corpus() {
    return {chain(2), chain(1), off_axis(), chain(0), restated()};
}

Rows walk(const Arrivals& arrivals, std::size_t wave_size = 0) {
    SatisfiabilityChecker& checker = global_sat_checker();
    return antichain::running_antichain(
        arrivals,
        [&checker](const tlsf::Specification& from,
                   const tlsf::Specification& dest) {
            return tlsf_spec_implies(from, dest, checker).value_or(false);
        },
        wave_size);
}

std::vector<std::string> final_members(const Rows& rows) {
    std::vector<std::string> members = antichain::members_at(
        rows, rows.empty() ? 0.0 : rows.back().m_elapsed_s);
    std::sort(members.begin(), members.end());
    return members;
}

void test_walk_agrees_with_the_batch_filter() {
    const std::vector<tlsf::Specification> specs = corpus();
    const Rows rows = walk(arrivals_of(specs));

    const std::vector<tlsf::Specification> kept =
        tlsf_make_implication_filter(global_sat_checker())(specs);
    std::vector<std::string> expected;
    for (const tlsf::Specification& survivor : kept) {
        const auto found = std::find(specs.begin(), specs.end(), survivor);
        expected.push_back("cand" + std::to_string(found - specs.begin()));
    }
    std::sort(expected.begin(), expected.end());

    expect(final_members(rows) == expected,
           "running_antichain: the walk ends on the batch filter's survivors");
}

// The point of the wave: a stale snapshot plus a reconciliation of the wave's
// own survivors has to land exactly where one arrival at a time lands, or the
// concurrency has changed the answer rather than only the schedule.
void test_wave_size_does_not_change_the_log() {
    const Arrivals arrivals = arrivals_of(corpus());
    const Rows serial = walk(arrivals, 1);
    for (const std::size_t wave :
         {std::size_t{2}, std::size_t{3}, std::size_t{16}}) {
        const Rows concurrent = walk(arrivals, wave);
        bool same = serial.size() == concurrent.size();
        for (std::size_t i = 0; same && i < serial.size(); ++i) {
            same = serial[i].m_name == concurrent[i].m_name &&
                   serial[i].m_event == concurrent[i].m_event &&
                   serial[i].m_size == concurrent[i].m_size;
        }
        expect(same, "running_antichain: wave " + std::to_string(wave) +
                         " reproduces the serial event log");
    }
}

void test_a_strengthening_evicts_what_it_dominates() {
    const Rows rows = walk(arrivals_of(corpus()));
    const bool removed =
        std::any_of(rows.begin(), rows.end(), [](const auto& row) {
            return row.m_event == antichain::AntichainEvent::Remove;
        });
    expect(removed,
           "running_antichain: a later, stronger arrival removes an "
           "earlier member");
    expect(final_members(rows).size() == 2,
           "running_antichain: the chain collapses to one survivor beside the "
           "off-axis spec");
}

// Every prefix's answer has to come out of the one walk, which is what replaces
// a sweep per time cut.
void test_membership_replays_at_every_prefix() {
    const Arrivals arrivals = arrivals_of(corpus());
    const Rows whole = walk(arrivals);
    for (std::size_t cut = 1; cut <= arrivals.size(); ++cut) {
        std::vector<std::string> replayed =
            antichain::members_at(whole, static_cast<double>(cut));
        std::sort(replayed.begin(), replayed.end());
        const Arrivals prefix(
            arrivals.begin(),
            arrivals.begin() + static_cast<std::ptrdiff_t>(cut));
        expect(replayed == final_members(walk(prefix)),
               "running_antichain: membership at cut " + std::to_string(cut) +
                   " matches a walk over that prefix alone");
    }
}

}  // namespace

void run_running_antichain_tests() {
    test_walk_agrees_with_the_batch_filter();
    test_wave_size_does_not_change_the_log();
    test_a_strengthening_evicts_what_it_dominates();
    test_membership_replays_at_every_prefix();
}
