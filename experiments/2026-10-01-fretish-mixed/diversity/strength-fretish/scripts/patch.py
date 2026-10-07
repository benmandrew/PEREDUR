import re
p = "src/compare.cpp"; s = open(p).read()
old = """    run_and_report(repair_meta, ideal_names,
                   [&](std::size_t rep, std::size_t ide) {
                       return [&, rep, ide] {
                           return classify(
                               spec_implies(repairs[rep].second.spec,
                                            ideals[ide].second, checker),
                               spec_implies(ideals[ide].second,
                                            repairs[rep].second.spec, checker));
                       };
                   });"""
new = """    // fret-pooled patch: COMPARE_DIRECTIONS=fwd|rev skips the other
    // direction, which the caller has already refuted on a sampled word, and
    // every pair's raw verdicts go to stderr as DIR lines.
    const char* dirs_env = std::getenv("COMPARE_DIRECTIONS");
    const std::string dirs = dirs_env != nullptr ? dirs_env : "both";
    const bool do_fwd = dirs != "rev";
    const bool do_rev = dirs != "fwd";
    static std::mutex dir_mutex;
    const auto tri = [](std::optional<bool> v) {
        return v.has_value() ? (*v ? "1" : "0") : "?";
    };
    run_and_report(repair_meta, ideal_names,
                   [&](std::size_t rep, std::size_t ide) {
                       return [&, rep, ide] {
                           const std::optional<bool> fwd =
                               do_fwd ? spec_implies(repairs[rep].second.spec,
                                                     ideals[ide].second, checker)
                                      : std::optional<bool>(false);
                           const std::optional<bool> rev =
                               do_rev ? spec_implies(ideals[ide].second,
                                                     repairs[rep].second.spec,
                                                     checker)
                                      : std::optional<bool>(false);
                           {
                               const std::lock_guard<std::mutex> lock(dir_mutex);
                               std::cerr << "DIR\\t" << repairs[rep].first << "\\t"
                                         << ideals[ide].first << "\\t" << tri(fwd)
                                         << "\\t" << tri(rev) << "\\n";
                           }
                           return classify(fwd, rev);
                       };
                   });"""
assert old in s
s = s.replace(old, new)
s = s.replace('#include "bounded_async.hpp"', '#include <cstdlib>\n#include <mutex>\n\n#include "bounded_async.hpp"', 1)
open(p, "w").write(s)
