#pragma once

// The steps both repair front ends take between loading their input and
// starting the search. Instantiated for Specification and tlsf::Specification.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"

// Screens @p original before anything is built from it, printing what it
// finds on stderr. False, after a `fatal:` line naming @p input_path, when an
// atom name is unsafe: that makes every realizability verdict meaningless
// rather than merely suspect, so it is checked first and stops the run.
//
// Then the input screen, since the seed population is copies of @p original
// and no filter ever sees them, so a property the gate will reject at the
// output is said at the start of the run. Then the ltlsynt budget screen on
// @p realizability: the query is memoised, so this is the call the first
// scoring pass was about to make anyway, timed.
template <typename Spec>
bool screen_original(const Spec& original, const std::string& input_path,
                     const std::vector<std::string>& inputs,
                     const std::vector<std::string>& outputs,
                     const std::function<std::optional<bool>()>& realizability,
                     const Config& cfg);

// Prints the seed where there is one, opens @p dashboard's log with the run's
// fixed facts, and says where its page is when one was written.
void announce_run(DashboardWriter& dashboard, const std::string& input_path,
                  const std::string& output_dir, const Config& cfg,
                  std::optional<std::size_t> seed,
                  const std::vector<std::string>& objectives,
                  const std::vector<std::string>& stages);
