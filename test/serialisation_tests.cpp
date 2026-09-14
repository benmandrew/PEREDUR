#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "requirement.hpp"
#include "serialisation.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

std::string write_temp_spec(const std::string& contents) {
    std::string path = "/tmp/peredur-spec-XXXXXX";
    const int file_descriptor = mkstemp(path.data());
    expect(file_descriptor >= 0,
           "atom-prefix: failed to create temp spec file");
    close(file_descriptor);
    std::ofstream out(path);
    expect(out.good(), "atom-prefix: failed to open temp spec file");
    out << contents;
    return path;
}

// out_atom "GF" is the formerly-dangerous case (lexed as G F operators);
// "STATE_FAULT" is a benign uppercase name that used to only warn.
const char* const k_atom_spec_json = R"JSON({
  "assumptions": [],
  "guarantees": [
    {"condition": "c", "response": "GF", "condition-type": "continual",
     "timing": {"type": "Immediately"}},
    {"condition": "c", "response": "STATE_FAULT", "condition-type": "continual",
     "timing": {"type": "Immediately"}}
  ],
  "in_atoms": ["c"],
  "out_atoms": ["GF", "STATE_FAULT"]
})JSON";

void test_load_serialise_preserves_original_atom_names() {
    const std::string path = write_temp_spec(k_atom_spec_json);
    const Specification spec = load_specification(path);
    std::remove(path.c_str());
    nlohmann::json jobj;
    to_json(jobj, spec);
    expect(jobj.dump().find("iap_") == std::string::npos,
           "atom-prefix: serialised spec must not leak the internal prefix");
    expect(jobj.at("out_atoms").at(0) == "GF" &&
               jobj.at("out_atoms").at(1) == "STATE_FAULT",
           "atom-prefix: out_atoms should round-trip to original names");
    expect(jobj.at("guarantees").at(0).at("response") == "GF",
           "atom-prefix: response 'GF' should round-trip to original name");
}

void test_load_tags_atoms_internally() {
    const std::string path = write_temp_spec(k_atom_spec_json);
    const Specification spec = load_specification(path);
    std::remove(path.c_str());
    expect(spec.m_in_atoms.size() == 1 && spec.m_in_atoms[0] == "iap_c",
           "atom-prefix: in-memory in_atom should carry the internal prefix");
    expect(spec.m_out_atoms.size() == 2 && spec.m_out_atoms[0] == "iap_GF" &&
               spec.m_out_atoms[1] == "iap_STATE_FAULT",
           "atom-prefix: in-memory out_atoms should carry the internal prefix");
    expect(spec.m_guarantees.size() == 2,
           "atom-prefix: expected two guarantees after load");
    expect(spec.m_guarantees[0].m_response.to_string() == "iap_GF",
           "atom-prefix: in-memory response formula should carry the prefix");
    expect(spec.m_guarantees[0].m_ltl.find("iap_GF") != std::string::npos,
           "atom-prefix: derived LTL should contain the prefixed atom");
}

void test_timing_immediately() {
    const Timing tim = timing::immediately();
    nlohmann::json jobj;
    timing::to_json(jobj, tim);
    expect(jobj.at("type") == "Immediately",
           "to_json(Timing): Immediately should serialise with type key");
    expect(jobj.get<Timing>() == tim,
           "from_json(Timing): Immediately round-trip should match");
}

void test_timing_next_timepoint() {
    const Timing tim = timing::next_timepoint();
    nlohmann::json jobj;
    timing::to_json(jobj, tim);
    expect(jobj.at("type") == "NextTimepoint",
           "to_json(Timing): NextTimepoint should serialise with type key");
    expect(jobj.get<Timing>() == tim,
           "from_json(Timing): NextTimepoint round-trip should match");
}

void test_timing_within_ticks() {
    const Timing tim = timing::within_ticks(7);
    nlohmann::json jobj;
    timing::to_json(jobj, tim);
    expect(jobj.at("type") == "WithinTicks",
           "to_json(Timing): WithinTicks should serialise with type key");
    expect(jobj.at("ticks") == 7,
           "to_json(Timing): WithinTicks should serialise ticks value");
    expect(jobj.get<Timing>() == tim,
           "from_json(Timing): WithinTicks round-trip should match");
}

void test_timing_for_ticks() {
    const Timing tim = timing::for_ticks(3);
    nlohmann::json jobj;
    timing::to_json(jobj, tim);
    expect(jobj.at("type") == "ForTicks",
           "to_json(Timing): ForTicks should serialise with type key");
    expect(jobj.at("ticks") == 3,
           "to_json(Timing): ForTicks should serialise ticks value");
    expect(jobj.get<Timing>() == tim,
           "from_json(Timing): ForTicks round-trip should match");
}

void test_timing_after_ticks() {
    const Timing tim = timing::after_ticks(2);
    nlohmann::json jobj;
    timing::to_json(jobj, tim);
    expect(jobj.at("type") == "AfterTicks",
           "to_json(Timing): AfterTicks should serialise with type key");
    expect(jobj.at("ticks") == 2,
           "to_json(Timing): AfterTicks should serialise ticks value");
    expect(jobj.get<Timing>() == tim,
           "from_json(Timing): AfterTicks round-trip should match");
}

void test_timing_eventually() {
    const Timing tim = timing::eventually();
    nlohmann::json jobj;
    timing::to_json(jobj, tim);
    expect(jobj.at("type") == "Eventually",
           "to_json(Timing): Eventually should serialise with type key");
    expect(jobj.get<Timing>() == tim,
           "from_json(Timing): Eventually round-trip should match");
}

void test_timing_always() {
    const Timing tim = timing::always();
    nlohmann::json jobj;
    timing::to_json(jobj, tim);
    expect(jobj.at("type") == "Always",
           "to_json(Timing): Always should serialise with type key");
    expect(jobj.get<Timing>() == tim,
           "from_json(Timing): Always round-trip should match");
}

void test_requirement_round_trip() {
    const Requirement req(Formula("(P) & (Q)"), Formula("R"),
                          timing::within_ticks(3), ConditionType::Trigger);
    nlohmann::json jobj;
    to_json(jobj, req);
    const Requirement req2 = serialisation::requirement_from_json(jobj);
    expect(req == req2,
           "requirement_from_json: round-trip should preserve all fields");
}

void test_requirement_json_keys() {
    const Requirement req(Formula("t"), Formula("r"), timing::immediately(),
                          ConditionType::Continual);
    nlohmann::json jobj;
    to_json(jobj, req);
    expect(jobj.contains("condition"),
           "to_json(Requirement): should have condition key");
    expect(jobj.contains("condition-type"),
           "to_json(Requirement): should have condition-type key");
    expect(jobj.at("condition-type") == "continual",
           "to_json(Requirement): condition-type should be continual");
    expect(jobj.contains("response"),
           "to_json(Requirement): should have response key");
    expect(jobj.contains("timing"),
           "to_json(Requirement): should have timing key");
    expect(jobj.at("timing").at("type") == "Immediately",
           "to_json(Requirement): timing type should be embedded");
}

void test_requirement_json_keys_trigger() {
    const Requirement req(Formula("c"), Formula("r"), timing::immediately(),
                          ConditionType::Trigger);
    nlohmann::json jobj;
    to_json(jobj, req);
    expect(jobj.at("condition-type") == "trigger",
           "to_json(Requirement): condition-type should be trigger");
}

void test_requirement_weakenable_omitted_when_true() {
    const Requirement req(Formula("a"), Formula("b"), timing::immediately());
    nlohmann::json jobj;
    to_json(jobj, req);
    expect(!jobj.contains("weakenable"),
           "to_json(Requirement): weakenable key should be omitted for the "
           "default (weakenable) requirement");
}

void test_requirement_non_weakenable_round_trip() {
    const Requirement req(Formula("a"), Formula("b"), timing::immediately(),
                          ConditionType::Continual, false);
    nlohmann::json jobj;
    to_json(jobj, req);
    expect(jobj.contains("weakenable") && jobj.at("weakenable") == false,
           "to_json(Requirement): a locked requirement should emit "
           "weakenable=false");
    const Requirement req2 = serialisation::requirement_from_json(jobj);
    expect(!req2.m_weakenable,
           "requirement_from_json: locked status should round-trip");
    expect(req == req2,
           "requirement_from_json: locked round-trip should preserve all "
           "fields");
}

void test_requirement_weakenable_defaults_true_when_absent() {
    const nlohmann::json jobj = {{"condition", "a"},
                                 {"response", "b"},
                                 {"condition-type", "continual"},
                                 {"timing", {{"type", "Immediately"}}}};
    const Requirement req = serialisation::requirement_from_json(jobj);
    expect(req.m_weakenable,
           "requirement_from_json: absent weakenable key should default to "
           "true");
}

// JSON is where a specification stops being search state and becomes a
// document, and a deleted guarantee's meaning there is absence. Marking it
// instead would leave every reader that has never heard of the flag treating a
// requirement the repair removed as one it kept.
void test_specification_omits_removed_requirements() {
    std::vector<Requirement> guarantees = {
        Requirement(Formula("a"), Formula("b"), timing::immediately()),
        Requirement(Formula("c"), Formula("d"), timing::immediately())};
    guarantees[0].m_removed = true;
    const Specification spec({}, guarantees, {"a", "c"}, {"b", "d"});
    nlohmann::json jobj;
    to_json(jobj, spec);
    expect(jobj.at("guarantees").size() == 1,
           "to_json(Specification): a deleted guarantee is not written");
    expect(jobj.at("guarantees")[0].at("condition") == "c",
           "to_json(Specification): the surviving guarantee is the live one");
    for (const auto& req : jobj.at("guarantees")) {
        expect(!req.contains("removed"),
               "to_json(Specification): no removal flag crosses the JSON "
               "boundary");
    }
    Specification parsed;
    from_json(jobj, parsed);
    expect(parsed.m_guarantees.size() == 1 &&
               !parsed.m_guarantees.front().m_removed,
           "from_json(Specification): a written specification reloads with the "
           "deleted guarantee simply absent");
}

void test_removed_flag_is_part_of_requirement_identity() {
    const Requirement live(Formula("a"), Formula("b"), timing::immediately());
    Requirement deleted = live;
    deleted.m_removed = true;
    expect(!(live == deleted),
           "requirement identity: a deleted requirement is not equal to the "
           "live one it replaced");
    expect(std::hash<Requirement>{}(live) != std::hash<Requirement>{}(deleted),
           "requirement identity: the removal flag reaches the hash, so the "
           "fitness cache cannot serve a live score for a deleted guarantee");
    expect((live < deleted) != (deleted < live),
           "requirement identity: the two are strictly ordered");
}

void test_specification_round_trip() {
    const Specification spec(
        {Requirement(Formula("a"), Formula("b"), timing::immediately(),
                     ConditionType::Continual)},
        {Requirement(Formula("t"), Formula("r"), timing::for_ticks(2),
                     ConditionType::Trigger),
         Requirement(Formula("p"), Formula("q"), timing::after_ticks(1),
                     ConditionType::Continual)},
        {"a", "t", "p"}, {"b", "r", "q"});
    nlohmann::json jobj;
    to_json(jobj, spec);
    const Specification spec2 = jobj.get<Specification>();
    expect(spec == spec2,
           "from_json(Specification): round-trip should preserve full spec");
}

void test_specification_json_structure() {
    const Specification spec(
        {}, {Requirement(Formula("t"), Formula("r"), timing::immediately())},
        {"t"}, {"r"});
    nlohmann::json jobj;
    to_json(jobj, spec);
    expect(jobj.contains("assumptions"),
           "to_json(Specification): should have assumptions key");
    expect(jobj.contains("guarantees"),
           "to_json(Specification): should have guarantees key");
    expect(jobj.contains("in_atoms"),
           "to_json(Specification): should have in_atoms key");
    expect(jobj.contains("out_atoms"),
           "to_json(Specification): should have out_atoms key");
    expect(jobj.at("guarantees").size() == 1,
           "to_json(Specification): guarantee count should match");
    expect(!jobj.contains("fitness"),
           "to_json(Specification): should not have fitness key");
}

void test_scored_specification_without_fitness() {
    const serialisation::ScoredSpecification scored{
        Specification(
            {},
            {Requirement(Formula("t"), Formula("r"), timing::next_timepoint())},
            {"t"}, {"r"}),
        std::nullopt};
    nlohmann::json jobj;
    serialisation::to_json(jobj, scored);
    expect(!jobj.contains("fitness"),
           "ScoredSpecification: absent fitness should not appear in JSON");
    const auto scored2 = jobj.get<serialisation::ScoredSpecification>();
    expect(scored2.spec == scored.spec,
           "ScoredSpecification: spec round-trip should match");
    expect(
        !scored2.fitness.has_value(),
        "ScoredSpecification: fitness should remain absent after round-trip");
}

void test_scored_specification_with_fitness() {
    const serialisation::ScoredSpecification scored{
        Specification(
            {}, {Requirement(Formula("t"), Formula("r"), timing::eventually())},
            {"t"}, {"r"}),
        serialisation::FitnessRecord{
            0.72, {{"syntactic", 0.8, 0.5}, {"semantic", 0.65, 0.5}}}};
    nlohmann::json jobj;
    serialisation::to_json(jobj, scored);
    expect(jobj.contains("fitness"),
           "ScoredSpecification: present fitness should appear in JSON");
    expect(jobj.at("fitness").at("total") == 0.72,
           "ScoredSpecification: total fitness should serialise correctly");
    expect(jobj.at("fitness").at("components").size() == 2,
           "ScoredSpecification: component count should serialise correctly");
    const auto scored2 = jobj.get<serialisation::ScoredSpecification>();
    expect(scored2.spec == scored.spec,
           "ScoredSpecification: spec round-trip with fitness should match");
    if (!scored2.fitness.has_value()) {
        fail("ScoredSpecification: fitness should be present after round-trip");
    }
    const auto& frec = *scored2.fitness;
    expect(frec.total == 0.72,
           "ScoredSpecification: total fitness round-trip should match");
    expect(frec.components.size() == 2,
           "ScoredSpecification: component count round-trip should match");
    expect(frec.components[0].name == "syntactic",
           "ScoredSpecification: component name round-trip should match");
    expect(frec.components[0].score == 0.8,
           "ScoredSpecification: component score round-trip should match");
    expect(frec.components[0].weight == 0.5,
           "ScoredSpecification: component weight round-trip should match");
}

// --- Scopes and modes ----------------------------------------------------

// Absence means Global, so a specification written before scopes existed reads
// back exactly as it did and writes back byte for byte.
void test_scope_omitted_when_global() {
    const Requirement req(Formula("a"), Formula("b"), timing::immediately());
    nlohmann::json jobj;
    to_json(jobj, req);
    expect(!jobj.contains("scope"),
           "to_json(Requirement): the scope key should be omitted for a global "
           "requirement");
}

void test_scope_round_trip() {
    for (const ScopeKind kind :
         {ScopeKind::In, ScopeKind::NotIn, ScopeKind::Before, ScopeKind::After,
          ScopeKind::OnlyIn, ScopeKind::OnlyBefore, ScopeKind::OnlyAfter}) {
        const Requirement req(Formula("a"), Formula("b"), timing::immediately(),
                              ConditionType::Continual, true, false,
                              Scope{kind, "roll_hold"});
        nlohmann::json jobj;
        to_json(jobj, req);
        expect(jobj.contains("scope"),
               "to_json(Requirement): a non-global scope must be written");
        expect(jobj.at("scope").at("mode") == "roll_hold",
               "to_json(Requirement): the mode must survive serialisation");
        const Requirement back = serialisation::requirement_from_json(jobj);
        expect(back == req, "Requirement: a scope must survive a round trip");
    }
}

void test_scope_defaults_to_global_when_absent() {
    const nlohmann::json jobj = {{"condition", "a"},
                                 {"condition-type", "continual"},
                                 {"response", "b"},
                                 {"timing", {{"type", "Immediately"}}}};
    const Requirement req = serialisation::requirement_from_json(jobj);
    expect(req.m_scope.is_global(),
           "requirement_from_json: an absent scope must read as Global");
}

void test_modes_omitted_when_empty() {
    const Specification spec({}, {}, {"a"}, {"b"});
    nlohmann::json jobj;
    to_json(jobj, spec);
    expect(!jobj.contains("modes"),
           "to_json(Specification): the modes key should be omitted when no "
           "modes are declared");
}

void test_modes_round_trip() {
    const Specification spec({}, {}, {"a"}, {"b"}, {"roll_hold", "nav"});
    nlohmann::json jobj;
    to_json(jobj, spec);
    Specification back;
    from_json(jobj, back);
    const std::vector<std::string> expected = {"roll_hold", "nav"};
    expect(back.m_modes == expected,
           "Specification: the modes list must survive a round trip");
}

// A mode is not an atom of either side, so a scope naming one that was never
// declared would fall through to the output half of ltlsynt's partition, where
// the synthesised system can discharge the scope by never entering the mode.
void test_undeclared_mode_is_rejected() {
    const nlohmann::json jobj = {
        {"assumptions", nlohmann::json::array()},
        {"guarantees",
         {{{"condition", "a"},
           {"condition-type", "continual"},
           {"response", "b"},
           {"timing", {{"type", "Immediately"}}},
           {"scope", {{"type", "In"}, {"mode", "roll_hold"}}}}}},
        {"in_atoms", {"a"}},
        {"out_atoms", {"b"}}};
    const auto err = validate_specification_json(jobj);
    expect(err.has_value(),
           "validate_specification_json: a scope over an undeclared mode must "
           "be rejected");
}

void test_mode_shared_with_an_atom_is_rejected() {
    const nlohmann::json jobj = {{"assumptions", nlohmann::json::array()},
                                 {"guarantees", nlohmann::json::array()},
                                 {"in_atoms", {"roll_hold"}},
                                 {"out_atoms", {"b"}},
                                 {"modes", {"roll_hold"}}};
    const auto err = validate_specification_json(jobj);
    expect(err.has_value(),
           "validate_specification_json: a name declared as both a mode and an "
           "atom must be rejected");
}

void test_global_scope_with_a_mode_is_rejected() {
    const nlohmann::json jobj = {
        {"assumptions", nlohmann::json::array()},
        {"guarantees",
         {{{"condition", "a"},
           {"condition-type", "continual"},
           {"response", "b"},
           {"timing", {{"type", "Immediately"}}},
           {"scope", {{"type", "Global"}, {"mode", "roll_hold"}}}}}},
        {"in_atoms", {"a"}},
        {"out_atoms", {"b"}},
        {"modes", {"roll_hold"}}};
    const auto err = validate_specification_json(jobj);
    expect(err.has_value(),
           "validate_specification_json: a Global scope naming a mode must be "
           "rejected rather than silently dropping it");
}

void test_scoped_requirement_needs_a_mode() {
    const nlohmann::json jobj = {{"assumptions", nlohmann::json::array()},
                                 {"guarantees",
                                  {{{"condition", "a"},
                                    {"condition-type", "continual"},
                                    {"response", "b"},
                                    {"timing", {{"type", "Immediately"}}},
                                    {"scope", {{"type", "In"}}}}}},
                                 {"in_atoms", {"a"}},
                                 {"out_atoms", {"b"}}};
    const auto err = validate_specification_json(jobj);
    expect(err.has_value(),
           "validate_specification_json: a non-Global scope with no mode must "
           "be rejected");
}

}  // namespace

void run_serialisation_tests() {
    test_timing_immediately();
    test_timing_next_timepoint();
    test_timing_within_ticks();
    test_timing_for_ticks();
    test_timing_after_ticks();
    test_timing_eventually();
    test_timing_always();
    test_requirement_round_trip();
    test_requirement_json_keys();
    test_requirement_json_keys_trigger();
    test_requirement_weakenable_omitted_when_true();
    test_requirement_non_weakenable_round_trip();
    test_requirement_weakenable_defaults_true_when_absent();
    test_specification_omits_removed_requirements();
    test_removed_flag_is_part_of_requirement_identity();
    test_specification_round_trip();
    test_specification_json_structure();
    test_scored_specification_without_fitness();
    test_scored_specification_with_fitness();
    test_load_serialise_preserves_original_atom_names();
    test_load_tags_atoms_internally();
    test_scope_omitted_when_global();
    test_scope_round_trip();
    test_scope_defaults_to_global_when_absent();
    test_modes_omitted_when_empty();
    test_modes_round_trip();
    test_undeclared_mode_is_rejected();
    test_mode_shared_with_an_atom_is_rejected();
    test_global_scope_with_a_mode_is_rejected();
    test_scoped_requirement_needs_a_mode();
}
