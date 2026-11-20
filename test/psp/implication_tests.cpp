// The PSP implication rule (src/psp/implication.cpp) against SPOT: each order
// and polarity table re-derived with ltlfilt, edges and non-edges alike, and
// requirement_implies checked on sampled pairs of requirements.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <future>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "filter/implication.hpp"
#include "ltl_oracle.hpp"
#include "prop_formula.hpp"
#include "psp/filter.hpp"
#include "psp/implication.hpp"
#include "psp/psp_fixtures.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "runner/black.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "psp_implication";

using psp::FormulaField;
using psp::Polarity;
using psp::PspScopeKind;
using psp_fixtures::k_scopes;
using psp_fixtures::measured_patterns;
using psp_fixtures::node_name;
using psp_fixtures::orders_json;
using psp_fixtures::over;
using psp_fixtures::pattern_label;

// ltl_implications over @p queries in batches, at most four ltlfilt runs at
// once (PLAN.md D10), since one run is capped at a minute.
std::vector<bool> capped_implications(const std::vector<Implication>& queries,
                                      std::size_t batch = 400) {
    constexpr std::size_t k_workers = 4;
    std::vector<bool> holds;
    holds.reserve(queries.size());
    std::deque<std::future<std::vector<bool>>> running;
    const auto drain_one = [&running, &holds]() {
        const std::vector<bool> part = running.front().get();
        running.pop_front();
        holds.insert(holds.end(), part.begin(), part.end());
    };
    for (std::size_t start = 0; start < queries.size(); start += batch) {
        const std::size_t end = std::min(start + batch, queries.size());
        std::vector<Implication> part(
            queries.begin() + static_cast<std::ptrdiff_t>(start),
            queries.begin() + static_cast<std::ptrdiff_t>(end));
        if (running.size() == k_workers) {
            drain_one();
        }
        running.push_back(
            std::async(std::launch::async, ltl_implications, std::move(part)));
    }
    while (!running.empty()) {
        drain_one();
    }
    return holds;
}

// One claim of a table, with the implication that decides it.
struct Claim {
    Implication m_query;
    bool m_claimed = false;
    std::string m_where;
};

// Every claim must agree with SPOT, an edge holding and a non-edge failing.
void expect_claims_hold(const std::vector<Claim>& claims,
                        const std::string& table) {
    std::vector<Implication> queries;
    queries.reserve(claims.size());
    for (const Claim& claim : claims) {
        queries.push_back(claim.m_query);
    }
    const std::vector<bool> holds = capped_implications(queries);
    for (std::size_t idx = 0; idx < claims.size(); ++idx) {
        if (claims[idx].m_claimed) {
            expect(holds[idx], table + " claims an edge that does not hold: " +
                                   claims[idx].m_where);
        } else {
            expect(!holds[idx], table + " is missing an edge that holds: " +
                                    claims[idx].m_where);
        }
    }
}

std::vector<Claim> pattern_claims(const std::vector<psp::Pattern>& patterns,
                                  PspScopeKind scope) {
    std::vector<Claim> claims;
    for (const psp::Pattern& stronger : patterns) {
        for (const psp::Pattern& weaker : patterns) {
            if (stronger == weaker) {
                continue;
            }
            claims.push_back(
                {{over(stronger, scope).m_ltl, over(weaker, scope).m_ltl},
                 psp::pattern_implies(stronger, weaker,
                                      psp::scope_class(scope)),
                 pattern_label(stronger) + " => " + pattern_label(weaker) +
                     " under " + psp::scope_name(scope)});
        }
    }
    return claims;
}

// --- The tables against SPOT ----------------------------------------------

// Every ordered pair of distinct patterns at counts 1 to 4, under every scope:
// pattern_implies must claim exactly the implications ltlfilt proves. The
// edge counts are those of `pattern_order` in docs/dev/psp-orders.json.
TEST(psp_pattern_order_is_pinned) {
    const std::vector<psp::Pattern> patterns = measured_patterns();
    const nlohmann::json measured = orders_json().at("pattern_order");
    std::vector<Claim> claims;
    for (const PspScopeKind scope : k_scopes) {
        const std::vector<Claim> cell = pattern_claims(patterns, scope);
        const auto edges = static_cast<std::size_t>(
            std::count_if(cell.begin(), cell.end(),
                          [](const Claim& claim) { return claim.m_claimed; }));
        expect(edges == measured.at(psp::scope_name(scope)).size(),
               std::string("pattern order: ") + psp::scope_name(scope) +
                   " has " + std::to_string(edges) +
                   " edges, docs/dev/psp-orders.json records " +
                   std::to_string(measured.at(psp::scope_name(scope)).size()));
        claims.insert(claims.end(), cell.begin(), cell.end());
    }
    expect_claims_hold(claims, "pattern_implies");
}

// The count relations, measured at 1 to 4, extended to larger counts: every
// pair among the base patterns and the bounded ones at counts 5 and 7, under
// one scope of each class.
TEST(psp_pattern_order_extends_to_larger_counts) {
    std::vector<psp::Pattern> patterns = {
        psp::pattern::Absence{}, psp::pattern::Universality{},
        psp::pattern::Existence{}, psp::pattern::Precedence{},
        psp::pattern::Response{}};
    for (const std::size_t count : {5, 7}) {
        patterns.emplace_back(psp::pattern::BoundedResponse{count});
        patterns.emplace_back(psp::pattern::BoundedInvariance{count});
        patterns.emplace_back(psp::pattern::BoundedExistence{count});
        patterns.emplace_back(psp::pattern::BoundedRecurrence{count});
    }
    std::vector<Claim> claims;
    for (const PspScopeKind scope :
         {PspScopeKind::Globally, PspScopeKind::Before,
          PspScopeKind::Between}) {
        const std::vector<Claim> cell = pattern_claims(patterns, scope);
        claims.insert(claims.end(), cell.begin(), cell.end());
    }
    expect_claims_hold(claims, "pattern_implies");
}

// Every pattern at counts 1 to 4 and every ordered pair of distinct scopes:
// scope_implies must claim exactly the implications ltlfilt proves.
TEST(psp_scope_order_is_pinned) {
    std::vector<Claim> claims;
    for (const psp::Pattern& pattern : measured_patterns()) {
        for (const PspScopeKind stronger : k_scopes) {
            for (const PspScopeKind weaker : k_scopes) {
                if (stronger == weaker) {
                    continue;
                }
                claims.push_back(
                    {{over(pattern, stronger).m_ltl,
                      over(pattern, weaker).m_ltl},
                     psp::scope_implies(stronger, weaker,
                                        psp::pattern_cell(pattern)),
                     pattern_label(pattern) + ": " + psp::scope_name(stronger) +
                         " => " + psp::scope_name(weaker)});
            }
        }
    }
    expect_claims_hold(claims, "scope_implies");
}

const char* field_name(FormulaField field) {
    switch (field) {
        case FormulaField::P:
            return "P";
        case FormulaField::S:
            return "S";
        case FormulaField::Q:
            return "Q";
        case FormulaField::R:
            return "R";
    }
    return "?";
}

const char* polarity_name(Polarity polarity) {
    switch (polarity) {
        case Polarity::Monotone:
            return "+";
        case Polarity::Antitone:
            return "-";
        case Polarity::None:
            return "none";
    }
    return "?";
}

// The cell's requirement with @p field set to @p value and the other fields
// at their atoms.
psp::Requirement with_field(const psp::Pattern& pattern, PspScopeKind scope,
                            FormulaField field, const std::string& value) {
    std::array<std::string, 4> fields = {"p", "s", "q", "r"};
    fields.at(static_cast<std::size_t>(field)) = value;
    return over(pattern, scope, fields[0], fields[1], fields[2], fields[3]);
}

// Each field's polarity in each cell at counts 1 to 4, from the two
// implications between the field at `f` and at the weaker `f | t`: monotone
// where only the first holds, antitone where only the second does, and none
// where neither does. Both holding would mean the field is not read. The
// polarities must also be those docs/dev/psp-orders.json records.
TEST(psp_polarity_is_pinned) {
    const nlohmann::json measured = orders_json().at("polarity");
    struct Cell {
        Polarity m_polarity = Polarity::None;
        std::string m_where;
    };
    std::vector<Cell> cells;
    std::vector<Implication> queries;
    for (const psp::Pattern& pattern : measured_patterns()) {
        for (const PspScopeKind scope : k_scopes) {
            const nlohmann::json& recorded =
                measured.at(node_name(pattern, scope));
            for (const FormulaField field :
                 {FormulaField::P, FormulaField::S, FormulaField::Q,
                  FormulaField::R}) {
                const std::optional<Polarity> polarity =
                    psp::field_polarity(field, pattern, scope);
                const std::string where = std::string(field_name(field)) +
                                          " in " + node_name(pattern, scope);
                expect(polarity.has_value() ==
                           recorded.contains(field_name(field)),
                       "polarity: whether the cell reads " + where +
                           " disagrees with docs/dev/psp-orders.json");
                if (!polarity.has_value()) {
                    continue;
                }
                expect(
                    recorded.at(field_name(field)) == polarity_name(*polarity),
                    "polarity: " + where +
                        " disagrees with docs/dev/psp-orders.json");
                const std::string narrow =
                    with_field(pattern, scope, field, "f").m_ltl;
                const std::string wide =
                    with_field(pattern, scope, field, "f | t").m_ltl;
                queries.push_back({narrow, wide});
                queries.push_back({wide, narrow});
                cells.push_back({*polarity, where});
            }
        }
    }
    const std::vector<bool> holds = capped_implications(queries);
    for (std::size_t idx = 0; idx < cells.size(); ++idx) {
        const bool onward = holds[2 * idx];
        const bool back = holds[(2 * idx) + 1];
        const Polarity polarity = cells[idx].m_polarity;
        expect(onward == (polarity == Polarity::Monotone) &&
                   back == (polarity == Polarity::Antitone),
               std::string("polarity: ") + polarity_name(polarity) +
                   " claimed for " + cells[idx].m_where +
                   " but SPOT gives onward " + (onward ? "1" : "0") +
                   ", back " + (back ? "1" : "0"));
    }
}

// --- requirement_implies --------------------------------------------------

psp::Requirement requirement(const psp::Pattern& pattern, PspScopeKind scope,
                             const std::optional<std::string>& field_p,
                             const std::optional<std::string>& field_s,
                             const std::optional<std::string>& field_q = {},
                             const std::optional<std::string>& field_r = {}) {
    const auto formula = [](const std::optional<std::string>& text) {
        return text.has_value() ? std::optional<Formula>(Formula(*text))
                                : std::nullopt;
    };
    return {pattern,          scope,
            formula(field_p), formula(field_s),
            formula(field_q), formula(field_r)};
}

bool spot_implies(const psp::Requirement& stronger,
                  const psp::Requirement& weaker) {
    return ltl_implications({{stronger.m_ltl, weaker.m_ltl}}).front();
}

// A field one side does not read takes the other's value: universality reads
// no P, and response then holds for every P.
TEST(psp_requirement_implies_reads_absent_fields_as_equal) {
    const psp::Requirement always_x = requirement(
        psp::pattern::Universality{}, PspScopeKind::Globally, {}, "x");
    const psp::Requirement responds = requirement(
        psp::pattern::Response{}, PspScopeKind::Globally, "a", "x | y");
    expect(psp::requirement_implies(always_x, responds) &&
               spot_implies(always_x, responds),
           "psp requirement_implies: universality must imply a weaker "
           "response whatever its trigger");
    expect(!psp::requirement_implies(responds, always_x),
           "psp requirement_implies: response must not imply universality");
    const psp::Requirement never_a =
        requirement(psp::pattern::Absence{}, PspScopeKind::Globally, "a", {});
    const psp::Requirement at_most_once = requirement(
        psp::pattern::BoundedExistence{1}, PspScopeKind::Globally, "a", {});
    expect(psp::requirement_implies(never_a, at_most_once),
           "psp requirement_implies: absence must imply bounded existence");
}

// A pair that moves the pattern, the scope and two formulas, where the scope
// must move before the pattern does: globally implies between for bounded
// invariance, and not for bounded response.
TEST(psp_requirement_implies_chains_fields_in_any_order) {
    const psp::Requirement tight = requirement(
        psp::pattern::BoundedInvariance{3}, PspScopeKind::Globally, "a", "x");
    const psp::Requirement loose =
        requirement(psp::pattern::BoundedResponse{2}, PspScopeKind::Between,
                    "a & b", "x | y", "q", "r");
    expect(psp::requirement_implies(tight, loose),
           "psp requirement_implies: the five-field chain must be proved");
    expect(spot_implies(tight, loose),
           "psp requirement_implies: the five-field chain must hold");
    expect(!psp::requirement_implies(loose, tight),
           "psp requirement_implies: the converse must not be proved");
    // The count relations hold beyond the measured counts.
    const psp::Requirement within_ten = requirement(
        psp::pattern::BoundedResponse{10}, PspScopeKind::Globally, "a", "x");
    const psp::Requirement within_twenty = requirement(
        psp::pattern::BoundedResponse{20}, PspScopeKind::Globally, "a", "x");
    expect(psp::requirement_implies(within_ten, within_twenty) &&
               !psp::requirement_implies(within_twenty, within_ten),
           "psp requirement_implies: bounded response must weaken with its "
           "count");
}

// A fixed raw-LTL requirement implies another only where the two lower
// identically, the rule reading no LTL.
TEST(psp_requirement_implies_fixed_only_when_identical) {
    const psp::Requirement fixed = psp::Requirement::fixed(Formula("G(a)"));
    const psp::Requirement same = psp::Requirement::fixed(Formula("G(a)"));
    const psp::Requirement other = psp::Requirement::fixed(Formula("F(a)"));
    const psp::Requirement pattern = requirement(
        psp::pattern::Universality{}, PspScopeKind::Globally, {}, "a");
    expect(psp::requirement_implies(fixed, same),
           "psp requirement_implies: a fixed requirement implies itself");
    expect(!psp::requirement_implies(fixed, other),
           "psp requirement_implies: fixed G a must stay unproved against F a");
    expect(pattern.m_ltl == fixed.m_ltl
               ? psp::requirement_implies(pattern, fixed)
               : !psp::requirement_implies(pattern, fixed),
           "psp requirement_implies: a pattern meets a fixed requirement only "
           "through an identical lowering");
}

// --- The specification-level checks ---------------------------------------

psp::Specification spec_of(std::vector<psp::Requirement> assumptions,
                           std::vector<psp::Requirement> guarantees) {
    return psp::Specification(std::move(assumptions), std::move(guarantees),
                              {"a"}, {"x", "y"});
}

psp::Requirement tombstone(psp::Requirement req) {
    req.m_removed = true;
    return req;
}

// Slot by slot, guarantees covariant and assumptions contravariant, with a
// tombstone read as `true`: it needs no cover and covers nothing.
TEST(psp_spec_implies_by_rule_reads_tombstones_as_true) {
    const psp::Requirement strong = requirement(
        psp::pattern::Universality{}, PspScopeKind::Globally, {}, "x");
    const psp::Requirement weak = requirement(
        psp::pattern::Universality{}, PspScopeKind::Globally, {}, "x | y");
    const psp::Requirement env =
        requirement(psp::pattern::Response{}, PspScopeKind::Globally, "a", "x");
    const psp::Requirement env_weak =
        requirement(psp::pattern::Existence{}, PspScopeKind::Globally, {}, "x");
    expect(psp::spec_implies_by_rule(spec_of({env}, {strong}),
                                     spec_of({env}, {weak})),
           "psp spec_implies_by_rule: a weaker guarantee is implied");
    expect(!psp::spec_implies_by_rule(spec_of({env}, {weak}),
                                      spec_of({env}, {strong})),
           "psp spec_implies_by_rule: a stronger guarantee is not");
    expect(psp::spec_implies_by_rule(spec_of({}, {strong}),
                                     spec_of({}, {tombstone(strong)})),
           "psp spec_implies_by_rule: a tombstoned guarantee needs no cover");
    expect(!psp::spec_implies_by_rule(spec_of({}, {tombstone(strong)}),
                                      spec_of({}, {strong})),
           "psp spec_implies_by_rule: a tombstone covers no guarantee");
    // A -> G is antitone in A: a stronger assumption weakens the
    // specification. Existence does not imply response, so it does not
    // cover the original assumption.
    const psp::Requirement env_strong = requirement(
        psp::pattern::Universality{}, PspScopeKind::Globally, {}, "x");
    expect(psp::spec_implies_by_rule(spec_of({env}, {strong}),
                                     spec_of({env_strong}, {strong})),
           "psp spec_implies_by_rule: a stronger assumption is implied");
    expect(!psp::spec_implies_by_rule(spec_of({env}, {strong}),
                                      spec_of({env_weak}, {strong})),
           "psp spec_implies_by_rule: an incomparable assumption is not");
    expect(psp::spec_implies_by_rule(spec_of({tombstone(env)}, {strong}),
                                     spec_of({env}, {strong})),
           "psp spec_implies_by_rule: a tombstoned assumption needs no cover");
}

// spec_implies settles with no query when the rule covers every guarantee,
// and still answers by query when it does not.
TEST(psp_spec_implies_uses_the_rule_residual) {
    const psp::Requirement strong = requirement(
        psp::pattern::BoundedResponse{1}, PspScopeKind::Globally, "a", "x");
    const psp::Requirement weak =
        requirement(psp::pattern::Response{}, PspScopeKind::Globally, "a", "x");
    SatisfiabilityChecker checker;
    const std::size_t settled = ImplicationFilterStats::n_residual_settled;
    expect(psp::spec_implies(spec_of({}, {strong}), spec_of({}, {weak}),
                             checker) == std::optional<bool>(true),
           "psp spec_implies: bounded response implies response");
    expect(ImplicationFilterStats::n_residual_settled == settled + 1,
           "psp spec_implies: the rule must settle the pair with no query");
    expect(psp::spec_implies(spec_of({}, {weak}), spec_of({}, {strong}),
                             checker) == std::optional<bool>(false),
           "psp spec_implies: response does not imply bounded response");
    // Two guarantees that imply a third only together: the rule covers the
    // second slot, and the residual query must find the first.
    const psp::Requirement from_x = requirement(
        psp::pattern::Universality{}, PspScopeKind::Globally, {}, "x");
    const psp::Requirement from_y = requirement(
        psp::pattern::Universality{}, PspScopeKind::Globally, {}, "y");
    const psp::Requirement both = requirement(
        psp::pattern::Universality{}, PspScopeKind::Globally, {}, "x & y");
    const psp::Requirement filler =
        requirement(psp::pattern::Existence{}, PspScopeKind::Globally, {}, "y");
    const std::size_t residual = ImplicationFilterStats::n_residual_queries;
    expect(psp::spec_implies(spec_of({}, {from_x, from_y}),
                             spec_of({}, {both, filler}),
                             checker) == std::optional<bool>(true),
           "psp spec_implies: a joint implication must be found by query");
    expect(ImplicationFilterStats::n_residual_queries == residual + 1,
           "psp spec_implies: the query must leave out the covered slot");
}

// --- Sampled pairs against SPOT -------------------------------------------

// The value pools of a sweep. A field a pattern or scope does not read takes
// index 0, so two requirements differ in it only where both read it.
struct SweepPools {
    std::vector<psp::Pattern> m_patterns;
    std::vector<PspScopeKind> m_scopes;
    std::array<std::vector<std::string>, 4> m_formulas;
};

struct SweepResult {
    // Indexed by how many fields apart a pair is, 0 unused.
    std::array<std::size_t, 7> m_asked{};
    std::array<std::size_t, 7> m_spot{};
    std::array<std::size_t, 7> m_proved{};
    std::vector<std::string> m_unsound;
    std::vector<std::string> m_missed;
};

using SweepIndex = std::array<std::size_t, 6>;

// Every requirement of the pools' product, with its index tuple.
void enumerate_requirements(const SweepPools& pools,
                            std::vector<SweepIndex>& all,
                            std::vector<psp::Requirement>& reqs) {
    for (std::size_t pat = 0; pat < pools.m_patterns.size(); ++pat) {
        const psp::Pattern& pattern = pools.m_patterns[pat];
        for (std::size_t sc = 0; sc < pools.m_scopes.size(); ++sc) {
            const PspScopeKind scope = pools.m_scopes[sc];
            const std::array<bool, 4> reads = {
                psp::pattern_uses_p(pattern), psp::pattern_uses_s(pattern),
                psp::scope_uses_q(scope), psp::scope_uses_r(scope)};
            std::array<std::size_t, 4> radix{};
            std::size_t combinations = 1;
            for (std::size_t field = 0; field < 4; ++field) {
                radix.at(field) =
                    reads.at(field) ? pools.m_formulas.at(field).size() : 1;
                combinations *= radix.at(field);
            }
            for (std::size_t code = 0; code < combinations; ++code) {
                SweepIndex idx = {pat, sc, 0, 0, 0, 0};
                std::array<std::optional<std::string>, 4> text;
                std::size_t rest = code;
                for (std::size_t field = 0; field < 4; ++field) {
                    idx.at(field + 2) = rest % radix.at(field);
                    rest /= radix.at(field);
                    if (reads.at(field)) {
                        text.at(field) =
                            pools.m_formulas.at(field).at(idx.at(field + 2));
                    }
                }
                all.push_back(idx);
                reqs.push_back(requirement(pattern, scope, text[0], text[1],
                                           text[2], text[3]));
            }
        }
    }
}

// Every `stride`th requirement of the pools' product against every
// requirement at most `max_apart` fields away, each pair sent to SPOT.
SweepResult sweep_requirement_implies(const SweepPools& pools,
                                      std::size_t stride,
                                      std::size_t max_apart) {
    std::vector<SweepIndex> all;
    std::vector<psp::Requirement> reqs;
    enumerate_requirements(pools, all, reqs);
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    std::vector<std::size_t> distances;
    for (std::size_t i = 0; i < all.size(); i += stride) {
        for (std::size_t j = 0; j < all.size(); ++j) {
            std::size_t apart = 0;
            for (std::size_t field = 0; field < 6; ++field) {
                apart += static_cast<std::size_t>(all[i].at(field) !=
                                                  all[j].at(field));
            }
            if (apart != 0 && apart <= max_apart) {
                pairs.emplace_back(i, j);
                distances.push_back(apart);
            }
        }
    }
    std::vector<Implication> queries;
    queries.reserve(pairs.size());
    for (const auto& [lhs, rhs] : pairs) {
        queries.push_back({reqs[lhs].m_ltl, reqs[rhs].m_ltl});
    }
    const std::vector<bool> holds = capped_implications(queries);
    SweepResult result;
    for (std::size_t k = 0; k < pairs.size(); ++k) {
        const psp::Requirement& stronger = reqs[pairs[k].first];
        const psp::Requirement& weaker = reqs[pairs[k].second];
        const bool proved = psp::requirement_implies(stronger, weaker);
        const std::size_t apart = distances[k];
        ++result.m_asked.at(apart);
        result.m_spot.at(apart) += static_cast<std::size_t>(holds[k]);
        result.m_proved.at(apart) += static_cast<std::size_t>(proved);
        if (proved && !holds[k]) {
            result.m_unsound.push_back(stronger.to_string() + " => " +
                                       weaker.to_string());
        } else if (!proved && holds[k]) {
            result.m_missed.push_back(stronger.to_string() + " => " +
                                      weaker.to_string());
        }
    }
    return result;
}

std::string first_of(const std::vector<std::string>& pairs) {
    return pairs.empty() ? std::string("none") : pairs.front();
}

std::size_t total(const std::array<std::size_t, 7>& counts) {
    std::size_t sum = 0;
    for (const std::size_t count : counts) {
        sum += count;
    }
    return sum;
}

void report(const char* name, const SweepResult& result,
            std::size_t max_apart) {
    std::cerr << name << ": " << total(result.m_asked) << " asked, "
              << total(result.m_spot) << " SPOT implications, "
              << total(result.m_proved) << " proved, "
              << result.m_unsound.size() << " unsound, "
              << result.m_missed.size() << " missed\n";
    for (std::size_t apart = 1; apart <= max_apart; ++apart) {
        std::cerr << "  " << apart << " apart: " << result.m_asked.at(apart)
                  << " asked, " << result.m_spot.at(apart)
                  << " SPOT implications, " << result.m_proved.at(apart)
                  << " proved\n";
    }
}

SweepPools base_pools() {
    return {
        {psp::pattern::Absence{}, psp::pattern::Universality{},
         psp::pattern::Existence{}, psp::pattern::Precedence{},
         psp::pattern::Response{}, psp::pattern::BoundedResponse{1},
         psp::pattern::BoundedResponse{2}, psp::pattern::BoundedInvariance{1},
         psp::pattern::BoundedInvariance{2}, psp::pattern::BoundedExistence{1},
         psp::pattern::BoundedRecurrence{1},
         psp::pattern::BoundedRecurrence{2}},
        {k_scopes.begin(), k_scopes.end()},
        {{{"a", "a & b", "a | b"},
          {"x", "x | y", "x & y"},
          {"q", "q & u"},
          {"r", "r | v"}}}};
}

// A sample of requirements against every requirement one or two fields away,
// each pair sent to SPOT: every pair the rule proves must hold.
TEST(psp_requirement_implies_is_sound) {
    const SweepResult result = sweep_requirement_implies(base_pools(), 37, 2);
    expect(result.m_unsound.empty(),
           "psp requirement_implies proved " +
               std::to_string(result.m_unsound.size()) +
               " non-implications, first " + first_of(result.m_unsound));
    expect(result.m_missed.empty(), "psp requirement_implies missed " +
                                        std::to_string(result.m_missed.size()) +
                                        " implications, first " +
                                        first_of(result.m_missed));
    expect(total(result.m_proved) > 0 &&
               total(result.m_proved) < total(result.m_asked),
           "psp requirement_implies: the sample must prove some pairs, not "
           "all");
}

// Opt-in, since it takes minutes: `peredur_tests
// psp_requirement_implies_sweep`. Wider pools than the test above, with an
// unrelated atom in each formula field (a different one for Q and for R, so no
// scope is empty) and every bounded pattern at counts 1 to 3, and pairs up to
// three fields apart. PEREDUR_PSP_SWEEP_STRIDE overrides the stride of 41. It
// prints the recall at each distance; docs/dev/psp.md ("Implication rule")
// records it.
TEST_IN("psp_requirement_implies_sweep", psp_requirement_implies_wide_sweep) {
    SweepPools pools = {{psp::pattern::Absence{}, psp::pattern::Universality{},
                         psp::pattern::Existence{}, psp::pattern::Precedence{},
                         psp::pattern::Response{}},
                        {k_scopes.begin(), k_scopes.end()},
                        {{{"a", "a & b", "a | b", "c"},
                          {"x", "x | y", "x & y", "z"},
                          {"q", "q & u", "q | u", "w"},
                          {"r", "r | v", "r & v", "o"}}}};
    for (std::size_t count = 1; count <= 3; ++count) {
        pools.m_patterns.emplace_back(psp::pattern::BoundedResponse{count});
        pools.m_patterns.emplace_back(psp::pattern::BoundedInvariance{count});
        pools.m_patterns.emplace_back(psp::pattern::BoundedExistence{count});
        pools.m_patterns.emplace_back(psp::pattern::BoundedRecurrence{count});
    }
    std::size_t stride = 41;
    if (const char* text = std::getenv("PEREDUR_PSP_SWEEP_STRIDE")) {
        stride = std::stoul(text);
    }
    const SweepResult result = sweep_requirement_implies(pools, stride, 3);
    report("psp requirement_implies sweep", result, 3);
    if (std::getenv("PEREDUR_SWEEP_MISSES") != nullptr) {
        for (const std::string& miss : result.m_missed) {
            std::cerr << "missed: " << miss << '\n';
        }
    }
    expect(result.m_unsound.empty(),
           "psp requirement_implies proved " +
               std::to_string(result.m_unsound.size()) +
               " non-implications, first " + first_of(result.m_unsound));
    // No miss at any distance. A miss means the rules lost an edge, or a pool
    // gained a value that empties a scope (Q and R one atom), which makes a
    // requirement vacuous in a way no field order sees.
    expect(result.m_missed.empty(),
           "psp requirement_implies sweep: expected no misses, found " +
               std::to_string(result.m_missed.size()) + ", first " +
               first_of(result.m_missed));
}

}  // namespace
