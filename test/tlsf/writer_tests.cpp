#include <string>
#include <string_view>
#include <vector>

#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"
#include "tlsf/writer.hpp"

namespace {

constexpr std::string_view k_test_suite = "tlsf_writer";

// parse(write(parse(text))) must equal parse(text) under operator==.
void expect_round_trip(const std::string& text, const std::string& label) {
    const tlsf::Specification original = tlsf::parse(text);
    const tlsf::Specification reparsed = tlsf::parse(tlsf::write(original));
    expect(reparsed == original, "round-trip: " + label);
}

TEST(test_round_trips) {
    expect_round_trip(
        "INFO { TITLE: \"Arbiter\"; DESCRIPTION: \"d\";\n"
        "       SEMANTICS: Mealy,standard; TARGET: Mealy; }\n"
        "MAIN { INPUTS { req; } OUTPUTS { grant; }\n"
        "       ASSUME { G F req; }\n"
        "       GUARANTEE { G (req -> F grant); } }",
        "arbiter");

    expect_round_trip(
        "INFO { SEMANTICS: Moore,strict; }\n"
        "MAIN { INPUTS { a; b; } OUTPUTS { c; d; }\n"
        "       INITIALLY { !a; }\n"
        "       PRESET { d; }\n"
        "       REQUIRE { a -> b; }\n"
        "       ASSERT { c; }\n"
        "       GUARANTEE { X[2] c; F[0..2] d; } }",
        "all sections + bounded expansion");

    expect_round_trip(
        "INFO { SEMANTICS: Mealy; }\n"
        "MAIN { INPUTS { } OUTPUTS { p; } GUARANTEE { (p) U (p); } }",
        "until operator and empty inputs");
}

TEST(test_write_structure) {
    const tlsf::Specification spec = tlsf::parse(
        "INFO { TITLE: \"T\"; SEMANTICS: Mealy; }\n"
        "MAIN { INPUTS { r; } OUTPUTS { g; }\n"
        "       ASSUME { r; } GUARANTEE { g; } }");
    const std::string out = tlsf::write(spec);
    expect(out.find("INFO {") != std::string::npos, "write: has INFO block");
    expect(out.find("MAIN {") != std::string::npos, "write: has MAIN block");
    expect(out.find("INPUTS {") != std::string::npos, "write: has INPUTS");
    expect(out.find("OUTPUTS {") != std::string::npos, "write: has OUTPUTS");
    expect(out.find("ASSUME {") != std::string::npos, "write: has ASSUME");
    expect(out.find("GUARANTEE {") != std::string::npos,
           "write: has GUARANTEE");
    // Absent sections are omitted.
    expect(out.find("PRESET") == std::string::npos,
           "write: omits empty sections");
    expect(out.find("TITLE:") != std::string::npos, "write: emits title");
}

TEST(test_semantics_written) {
    const tlsf::Specification spec = tlsf::parse(
        "INFO { SEMANTICS: Moore,strict; }\nMAIN { GUARANTEE { g; } }");
    const std::string out = tlsf::write(spec);
    expect(out.find("Moore,Strict") != std::string::npos,
           "write: strict Moore semantics rendered");
    expect(tlsf::parse(out).m_semantics == tlsf::Semantics::MooreStrict,
           "write: semantics survives round-trip");
}

// Written output must follow the TLSF grammar: INFO entries carry no `;`
// terminator, and boolean connectives use the doubled `&&`/`||`.
TEST(test_write_conformance) {
    const tlsf::Specification spec = tlsf::parse(
        "INFO { TITLE: \"T\"; SEMANTICS: Mealy; }\n"
        "MAIN { OUTPUTS { a; b; } GUARANTEE { a & b; } }");
    const std::string out = tlsf::write(spec);
    expect(out.find("Mealy;") == std::string::npos,
           "write: INFO entries are not ';'-terminated");
    expect(out.find("&&") != std::string::npos,
           "write: conjunction uses the doubled && form");
    expect(tlsf::parse(out) == spec, "write: conformant output round-trips");
}

}  // namespace
