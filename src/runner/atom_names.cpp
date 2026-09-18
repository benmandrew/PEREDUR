#include "atom_names.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace runner {

namespace {

bool is_upper(char chr) {
    return std::isupper(static_cast<unsigned char>(chr)) != 0;
}

// SPOT eats the leading letter only when what follows could continue an
// identifier alphabetically; a digit leaves the name intact.
bool leads_with_operator(const std::string& name) {
    if (name.size() < 2) {
        return false;
    }
    if (name[0] != 'F' && name[0] != 'G' && name[0] != 'X') {
        return false;
    }
    const char next = name[1];
    return next == '_' || std::isalpha(static_cast<unsigned char>(next)) != 0;
}

std::optional<std::string> check(const std::string& name, bool is_input) {
    if (name.empty()) {
        return "an atom name is empty";
    }
    if (leads_with_operator(name)) {
        return "'" + name +
               "' begins with a temporal operator letter, so SPOT "
               "reads it as " +
               name[0] + "(" + name.substr(1) +
               "); rename it or prefix it with a letter";
    }
    if (!is_input) {
        return std::nullopt;
    }
    for (const char chr : name) {
        if (is_upper(chr)) {
            return "input '" + name +
                   "' contains an uppercase letter, which ltlsynt's --ins "
                   "never matches; the atom silently becomes an output and "
                   "the realizability verdict errs towards realizable. Use "
                   "lower_snake_case";
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::string> first_unsafe_atom_name(
    const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs) {
    for (const std::string& name : inputs) {
        if (std::optional<std::string> unsafe = check(name, true)) {
            return unsafe;
        }
    }
    for (const std::string& name : outputs) {
        if (std::optional<std::string> unsafe = check(name, false)) {
            return unsafe;
        }
    }
    return std::nullopt;
}

}  // namespace runner
