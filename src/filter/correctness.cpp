#include "filter/correctness.hpp"

#include <string>
#include <vector>

#include "filter/vacuity.hpp"
#include "filter/well_separation.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/specification.hpp"

namespace {

bool is_vacuous(const Specification& spec, SatisfiabilityChecker& sat) {
    return specification_is_vacuous(spec, sat);
}

bool is_vacuous(const tlsf::Specification& spec, SatisfiabilityChecker& sat) {
    return tlsf_is_vacuous(spec, sat);
}

}  // namespace

std::string input_screen_warning(const std::string& check_name) {
    return "warning: the input specification fails the " + check_name +
           " check.\n"
           "  It cannot be written as a repair of itself, so a repair of this "
           "run\n"
           "  is a descendant that fixes the property as well as the "
           "unrealizability.\n";
}

std::string input_screen_error_warning(const std::string& error) {
    return "warning: the input specification could not be screened: " + error +
           "\n"
           "  Whether it holds the correctness properties is unknown, so this "
           "run\n"
           "  cannot say whether a repair of it is a repair of the property "
           "too.\n";
}

template <typename Spec>
std::vector<CorrectnessCheckT<Spec>> correctness_checks(
    SatisfiabilityChecker& sat, RealizabilityChecker& real) {
    std::vector<CorrectnessCheckT<Spec>> checks;
    checks.push_back(
        {"vacuity", [&sat](const Spec& spec) { return !is_vacuous(spec, sat); },
         true});
    checks.push_back({"not-well-separated",
                      [&real](const Spec& spec) {
                          return !specification_is_not_well_separated(spec,
                                                                      real);
                      },
                      false});
    return checks;
}

template std::vector<CorrectnessCheckT<Specification>>
correctness_checks<Specification>(SatisfiabilityChecker&,
                                  RealizabilityChecker&);
template std::vector<CorrectnessCheckT<tlsf::Specification>>
correctness_checks<tlsf::Specification>(SatisfiabilityChecker&,
                                        RealizabilityChecker&);
