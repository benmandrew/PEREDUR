#include "formula_key.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "prop_formula.hpp"
#include "prop_formula/atoms.hpp"

namespace {

// Atoms in the order Formula::to_string renders them, first occurrence only,
// so that two formulae equal up to renaming produce the same sequence.
void collect_atoms(const Formula& formula, std::vector<std::string>& order,
                   std::unordered_set<std::string>& seen) {
    if (const auto name = formula.atom_name()) {
        if (!is_constant_atom(*name) && seen.insert(*name).second) {
            order.push_back(*name);
        }
        return;
    }
    if (const auto child = formula.unary_child()) {
        collect_atoms(*child, order, seen);
        return;
    }
    if (const auto children = formula.binary_children()) {
        collect_atoms(children->first, order, seen);
        collect_atoms(children->second, order, seen);
    }
}

Formula rename_atoms(const Formula& formula,
                     const std::unordered_map<std::string, std::string>& map) {
    return formula.rewrite_post_order(
        [&map](const Formula& node) -> std::optional<Formula> {
            const auto name = node.atom_name();
            if (!name) {
                return std::nullopt;
            }
            const auto found = map.find(*name);
            if (found == map.end()) {
                return std::nullopt;
            }
            return Formula::make_atom(found->second);
        });
}

// Parsing asserts on a malformed formula rather than reporting one, and these
// keys are an optimisation, so anything unparseable is keyed on itself: a
// missed collapse rather than a crash or a wrong answer. The same holds for a
// formula in a spelling the parser reads differently from its author -- SPOT's
// `Ffk1` lexes here as one atom -- since the reading is deterministic, two
// strings share a key only when they parse to one formula.
std::optional<Formula> try_parse(const std::string& ltl) {
    return ltl.empty() ? std::nullopt : Formula::try_parse(ltl);
}

// One memo per entry point, all append-only, so a reference handed out under
// the lock stays valid after it is released.
std::mutex g_mutex;
std::unordered_map<std::string, std::string> g_canonical;
std::unordered_map<std::string, std::string> g_renamed;
std::unordered_map<std::string, std::string> g_realizability;

const std::string& remember(std::unordered_map<std::string, std::string>& memo,
                            const std::string& key, std::string value) {
    const std::scoped_lock lock(g_mutex);
    return memo.emplace(key, std::move(value)).first->second;
}

}  // namespace

namespace formula_key {

const std::string& canonical(const std::string& ltl) {
    {
        const std::scoped_lock lock(g_mutex);
        const auto found = g_canonical.find(ltl);
        if (found != g_canonical.end()) {
            return found->second;
        }
    }
    const auto parsed = try_parse(ltl);
    return remember(g_canonical, ltl,
                    parsed ? parsed->canonical().to_string() : ltl);
}

const std::string& renamed(const std::string& ltl) {
    {
        const std::scoped_lock lock(g_mutex);
        const auto found = g_renamed.find(ltl);
        if (found != g_renamed.end()) {
            return found->second;
        }
    }
    const auto parsed = try_parse(ltl);
    if (!parsed) {
        return remember(g_renamed, ltl, ltl);
    }
    const Formula canonical_form = parsed->canonical();
    std::vector<std::string> order;
    std::unordered_set<std::string> seen;
    collect_atoms(canonical_form, order, seen);
    std::unordered_map<std::string, std::string> forward;
    forward.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        forward.emplace(order[i], "fk" + std::to_string(i));
    }
    return remember(g_renamed, ltl,
                    rename_atoms(canonical_form, forward).to_string());
}

const std::string& realizability(const std::string& ltl,
                                 const std::vector<std::string>& inputs,
                                 const std::vector<std::string>& outputs) {
    // The declared lists are constant across a run, so their sizes are all the
    // memo key needs of them.
    const std::string memo_key = ltl + "\x1f" + std::to_string(inputs.size()) +
                                 "\x1f" + std::to_string(outputs.size());
    {
        const std::scoped_lock lock(g_mutex);
        const auto found = g_realizability.find(memo_key);
        if (found != g_realizability.end()) {
            return found->second;
        }
    }
    const auto parsed = try_parse(ltl);
    if (!parsed) {
        return remember(g_realizability, memo_key, ltl);
    }
    const Formula canonical_form = parsed->canonical();
    std::vector<std::string> order;
    std::unordered_set<std::string> seen;
    collect_atoms(canonical_form, order, seen);
    const std::unordered_set<std::string> input_set(inputs.begin(),
                                                    inputs.end());
    const std::unordered_set<std::string> output_set(outputs.begin(),
                                                     outputs.end());
    std::unordered_map<std::string, std::string> forward;
    std::size_t n_inputs = 0;
    std::size_t n_outputs = 0;
    for (const std::string& name : order) {
        // An atom in neither list keeps its name. Renaming it would let it
        // collide with a signal of a side it does not belong to, which is the
        // one thing this partition is here to prevent.
        if (input_set.count(name) != 0) {
            forward.emplace(name, "fi" + std::to_string(n_inputs++));
        } else if (output_set.count(name) != 0) {
            forward.emplace(name, "fo" + std::to_string(n_outputs++));
        }
    }
    // The declared signals the formula never mentions are interchangeable
    // among themselves, so their count is all the key needs of them -- but it
    // does need it, since they are part of the alphabet ltlsynt plays over and
    // two formulae over different alphabets can differ in realizability.
    return remember(g_realizability, memo_key,
                    rename_atoms(canonical_form, forward).to_string() + "|" +
                        std::to_string(inputs.size() - n_inputs) + "|" +
                        std::to_string(outputs.size() - n_outputs));
}

}  // namespace formula_key
