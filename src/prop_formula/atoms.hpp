#pragma once

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "prop_formula.hpp"

// "true" and "false" are logical constants, not user atoms. The parser reads
// them as ordinary atoms (see Formula); they are special-cased downstream
// (requirement_to_ltl, condition_to_string, Formula::simplify) and are boolean
// constants to SPOT and black. Renaming or prefixing one would turn a constant
// into a free atom.
inline bool is_constant_atom(const std::string& name) {
    return name == "true" || name == "false";
}

// Adds the name of every atom in @p formula, temporal operators included, to
// @p out. The constants are atoms by convention (see Formula), so they are
// collected too. src/formula_key.cpp keeps its own walk, which needs the atoms
// in rendering order and without the constants.
inline void collect_atoms(const Formula& formula,
                          std::unordered_set<std::string>& out) {
    if (const std::optional<std::string> name = formula.atom_name()) {
        out.insert(*name);
        return;
    }
    if (const std::optional<Formula> child = formula.unary_child()) {
        collect_atoms(*child, out);
        return;
    }
    if (const std::optional<std::pair<Formula, Formula>> children =
            formula.binary_children()) {
        collect_atoms(children->first, out);
        collect_atoms(children->second, out);
    }
}
