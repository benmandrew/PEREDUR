#pragma once

/// @file correctness.hpp
/// @brief The correctness properties a written repair must hold, as one list
///        shared by both front ends.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"

/// One property a specification must hold to be written out as a repair.
///
/// The same row drives three consumers: the per-generation filter chain builds
/// a stage from it when @c per_generation is set, the final gate applies
/// @c admissible to every survivor regardless of that, and the input
/// screen applies it once to the specification the run starts from. A property
/// enforced in only the first of those leaks through anything the search did
/// not breed -- an elite, or the seed population -- which is what made a
/// per-generation-only filter insufficient (issues #73, #77).
template <typename Spec>
struct CorrectnessCheckT {
    /// Display name, identical to the per-generation stage's. The dashboard
    /// derives its stage list from these and `run_experiments.py` parses the
    /// filter report, so a rename is a breaking change to both.
    std::string name;
    /// True when @p spec is admissible under this check.
    std::function<bool(const Spec&)> admissible;
    /// Whether this check also runs as a per-generation stage. It governs
    /// search pressure alone: the gate ignores it. False for a check the gate
    /// and the input screen apply but the search does not: well-separation is
    /// one, the status objective scoring an ill-separated candidate down
    /// rather than a filter dropping it unscored.
    bool per_generation = false;
};

/// The FRETISH check type.
using CorrectnessCheck = CorrectnessCheckT<Specification>;

/// Returns the name of the first check @p spec fails, or nullopt if it passes
/// every one. Ordered as given, so callers pay the cheapest query first and a
/// rejected specification never reaches the expensive ones.
template <typename Spec>
std::optional<std::string> first_failing_check(
    const Spec& spec, const std::vector<CorrectnessCheckT<Spec>>& checks) {
    for (const CorrectnessCheckT<Spec>& check : checks) {
        if (!check.admissible(spec)) {
            return check.name;
        }
    }
    return std::nullopt;
}

/// What the input screen found, for the run manifest to record.
///
/// Process-global for the reason the tool-call counters are: the manifest is
/// written by a driver that no longer holds the specification, and on the TLSF
/// path the front end parses its own input, so there is no call frame holding
/// both the verdict and the writer. Set once, before the search starts.
struct InputScreen {
    /// Name of the check the run's input failed. Empty when it passed, and
    /// when no screen ran.
    inline static std::string failed_check;
    /// Why the screen could not finish, when an external tool raised. Empty
    /// otherwise. Recorded separately from `failed_check` because an empty
    /// `failed_check` otherwise reads as "passed every check", which a screen
    /// that never ran to completion has not established.
    inline static std::string error;
};

/// The warning printed when the input specification fails @p check_name.
///
/// A warning rather than a rejection. An input that fails a check cannot itself
/// be written as a repair -- the gate rejects it -- but a descendant that
/// repairs the property can be, and the search can reach one, so refusing to
/// start would foreclose a repair the tool can express (issue #77).
std::string input_screen_warning(const std::string& check_name);

/// The warning printed when the screen could not finish because a tool raised.
std::string input_screen_error_warning(const std::string& error);

/// Screens the run's input against @p checks, recording the verdict in
/// InputScreen and returning what the driver should print (empty when the input
/// passed every check).
///
/// A tool that raises here is reported rather than propagated. The screen is
/// advisory -- it warns and never rejects, for the reason input_screen_warning
/// gives -- so a raising `black` or `ltlsynt` call costs the verdict alone.
/// Letting it out would cost the whole run before the search started, and cost
/// it badly: both drivers screen outside any handler of their own, so the throw
/// escapes with neither a `fatal:` line nor a manifest to say what happened.
template <typename Spec>
std::string screen_input(const Spec& spec,
                         const std::vector<CorrectnessCheckT<Spec>>& checks) {
    try {
        if (const std::optional<std::string> failed =
                first_failing_check(spec, checks);
            failed.has_value()) {
            InputScreen::failed_check = *failed;
            return input_screen_warning(*failed);
        }
    } catch (const std::exception& exc) {
        InputScreen::error = exc.what();
        return input_screen_error_warning(exc.what());
    }
    return {};
}

/// The correctness checks, cheapest first, in one order and under one set of
/// names on both front ends: instantiated for Specification (the default) and
/// tlsf::Specification, whose vacuity test is tlsf_is_vacuous. The table is
/// read three ways -- the per-generation chain, the final gate, and the input
/// screen -- which is why it is the single source rather than three
/// hand-mirrored lists.
///
/// `vacuity` leads: its syntactic screen costs nothing and its `black` queries
/// are keyed per requirement, so a candidate bred from a scored parent pays
/// only for what mutation changed. `not-well-separated` is last because it is
/// the one check that can be cold at the gate -- with its per-generation stage
/// off, nothing in the run has warmed its `ltlsynt` query, whereas
/// realizability is a scored objective and so already memoised for everything
/// the final generation scored.
///
/// @param sat  Satisfiability checker (`black`); captured by reference into the
///             returned predicates and must outlive them
/// @param real Realizability checker (`ltlsynt`); likewise
template <typename Spec = Specification>
std::vector<CorrectnessCheckT<Spec>> correctness_checks(
    SatisfiabilityChecker& sat, RealizabilityChecker& real);
