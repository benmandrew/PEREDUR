#pragma once

#include <optional>
#include <string>

#include "prop_formula.hpp"

namespace psp::detail {

// A field as it sits inside a larger formula: an atom bare, anything else in
// parentheses. Formula::to_string leaves a binary root unwrapped ("(a) & (b)"),
// which `!` or `X` in front would bind to its left operand alone. The atoms of
// the reference lowering (scripts/psp_field_orders.py) print bare, so on atoms
// this lowering and the script's agree character for character.
inline std::string operand(const Formula& formula) {
    if (const std::optional<std::string> name = formula.atom_name()) {
        return *name;
    }
    return "(" + formula.to_string() + ")";
}

}  // namespace psp::detail
