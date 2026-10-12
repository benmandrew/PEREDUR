#include "tlsf/writer.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "prop_formula.hpp"
#include "tlsf/specification.hpp"

namespace tlsf {

namespace {

std::string escape_string(const std::string& value) {
    std::string result;
    for (const char character : value) {
        if (character == '"' || character == '\\') {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    return result;
}

std::string replace_all(std::string text, const std::string& pattern,
                        const std::string& replacement) {
    std::size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        text.replace(pos, pattern.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

// Formula::to_string() renders SPOT syntax with single `&`/`|`; TLSF's boolean
// connectives are the doubled `&&`/`||`. Operands are fully parenthesized, so
// each connective appears only as the delimited substring " & " / " | ".
std::string to_tlsf_expr(const Formula& formula) {
    return replace_all(replace_all(formula.to_string(), " & ", " && "), " | ",
                       " || ");
}

const char* machine_name(Semantics semantics) {
    switch (semantics) {
        case Semantics::MooreStandard:
        case Semantics::MooreStrict:
            return "Moore";
        case Semantics::MealyStandard:
        case Semantics::MealyStrict:
        default:
            return "Mealy";
    }
}

const char* semantics_name(Semantics semantics) {
    switch (semantics) {
        case Semantics::MealyStrict:
            return "Mealy,Strict";
        case Semantics::MooreStandard:
            return "Moore";
        case Semantics::MooreStrict:
            return "Moore,Strict";
        case Semantics::MealyStandard:
        default:
            return "Mealy";
    }
}

void write_signal_list(std::string& out, const std::string& name,
                       const std::vector<std::string>& signals) {
    out += "  " + name + " {\n";
    for (const std::string& signal : signals) {
        out += "    " + signal + ";\n";
    }
    out += "  }\n";
}

// Deleted conjuncts are not written. TLSF text is the boundary where a
// specification stops being search state and becomes a document, and a deleted
// conjunct's meaning there is absence; `parse(write(spec))` therefore returns
// the specification without them, which is what it says. A section left with
// nothing live is omitted, exactly as an absent one is.
void write_formula_section(std::string& out, const std::string& name,
                           const Section& section) {
    const std::vector<Formula> formulae = live_formulae(section);
    if (formulae.empty()) {
        return;
    }
    out += "  " + name + " {\n";
    for (const Formula& formula : formulae) {
        out += "    " + to_tlsf_expr(formula) + ";\n";
    }
    out += "  }\n";
}

// Appends a line for each formula of @p from with no equal partner left in
// @p against. A formula held twice on one side and once on the other is
// listed once.
void write_unmatched(std::string& out, const std::string& prefix,
                     const std::vector<Formula>& from,
                     const std::vector<Formula>& against) {
    std::vector<bool> used(against.size(), false);
    for (const Formula& formula : from) {
        bool matched = false;
        for (std::size_t i = 0; i < against.size() && !matched; ++i) {
            if (!used[i] && against[i] == formula) {
                used[i] = true;
                matched = true;
            }
        }
        if (!matched) {
            out += prefix + to_tlsf_expr(formula) + "\n";
        }
    }
}

}  // namespace

std::string write(const Specification& specification) {
    std::string out;
    // TLSF INFO entries carry no `;` terminator (unlike MAIN statements).
    out += "INFO {\n";
    out += "  TITLE:       \"" + escape_string(specification.m_title) + "\"\n";
    out += "  DESCRIPTION: \"" + escape_string(specification.m_description) +
           "\"\n";
    out += std::string("  SEMANTICS:   ") +
           semantics_name(specification.m_semantics) + "\n";
    out += std::string("  TARGET:      ") +
           machine_name(specification.m_semantics) + "\n";
    out += "}\n\n";

    out += "MAIN {\n";
    write_signal_list(out, "INPUTS", specification.m_inputs);
    write_signal_list(out, "OUTPUTS", specification.m_outputs);
    write_formula_section(out, "INITIALLY", specification.m_initially);
    write_formula_section(out, "PRESET", specification.m_preset);
    write_formula_section(out, "REQUIRE", specification.m_require);
    write_formula_section(out, "ASSUME", specification.m_assume);
    write_formula_section(out, "ASSERT", specification.m_assert);
    write_formula_section(out, "GUARANTEE", specification.m_guarantee);
    out += "}\n";
    return out;
}

std::string write_changes(const Specification& original,
                          const Specification& repair) {
    static const std::array<const char*, 6> k_names = {
        "INITIALLY", "PRESET", "REQUIRE", "ASSUME", "ASSERT", "GUARANTEE"};
    const std::array<const Section*, 6> before = sections_of(original);
    const std::array<const Section*, 6> after = sections_of(repair);
    std::string out;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const std::vector<Formula> old_formulae = live_formulae(*before[i]);
        const std::vector<Formula> new_formulae = live_formulae(*after[i]);
        const std::string name = std::string("  ") + k_names[i];
        write_unmatched(out, name + " - ", old_formulae, new_formulae);
        write_unmatched(out, name + " + ", new_formulae, old_formulae);
    }
    return out;
}

}  // namespace tlsf
