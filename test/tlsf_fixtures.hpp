#pragma once

// Builders shared by the TLSF suites.

#include <string>

#include "runner/spot.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

/// Wraps a MAIN body (INPUTS, OUTPUTS and sections) in a minimal document.
inline std::string tlsf_doc(const std::string& main_body,
                            const std::string& semantics = "Mealy") {
    return "INFO { SEMANTICS: " + semantics + "; }\nMAIN {\n" + main_body +
           "\n}\n";
}

inline tlsf::Specification parse_main(const std::string& main_body,
                                      const std::string& semantics = "Mealy") {
    return tlsf::parse(tlsf_doc(main_body, semantics));
}

/// A two-client mutual-exclusion GR(1) arbiter that is unrealizable without a
/// request-fairness assumption: the environment can hold both requests low
/// forever, so the system cannot satisfy `G F g0` and `G F g1` while honouring
/// `G(g -> r)`.
inline constexpr const char* k_unrealizable_arbiter =
    "INFO { SEMANTICS: Mealy; }\n"
    "MAIN {\n"
    "  INPUTS { r0; r1; }\n"
    "  OUTPUTS { g0; g1; }\n"
    "  GUARANTEE {\n"
    "    G (g0 -> r0);\n"
    "    G (g1 -> r1);\n"
    "    G !(g0 & g1);\n"
    "    G F g0;\n"
    "    G F g1;\n"
    "  }\n"
    "}\n";

inline bool is_realizable(const tlsf::Specification& spec) {
    // No timeout is set in the tests, so every query is decided; value_or's
    // argument is unreachable rather than a policy choice.
    return global_real_checker()
        .check_realizability_ltl(spec.to_ltl(), spec.m_inputs, spec.m_outputs)
        .value_or(false);
}
