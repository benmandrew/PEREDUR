// Scratch tool: fingerprint FRETISH repairs over targeted lasso words.
// usage: fpdraw words max_prefix max_cycle seed max_steps threads spec list
// Each word aims at one repair drawn uniformly from <list>: a random lasso is
// improved by greedy bit flips (with noise) until it satisfies every live
// assumption and guarantee of that repair, so the word lies in the repair's
// language non-vacuously. Words that never get there are dropped. Then every
// listed repair is fingerprinted over the kept words, printed as fpcal does.
// Stats go to stderr.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fingerprint/lasso.hpp"
#include "requirement.hpp"
#include "serialisation.hpp"

namespace {

// FPDRAW_UNGLUE=1 rewrites a temporal letter glued to an atom (`Xiap_m`, as
// start_of_mode emits) into `X(iap_m)`, the reading SPOT gives it; PEREDUR's
// own parser reads the glued form as a separate, never-true atom.
std::string spelled(const Specification& spec) {
    static const bool unglue = [] {
        const char* v = std::getenv("FPDRAW_UNGLUE");
        return v != nullptr && std::string(v) == "1";
    }();
    std::string ltl = spec.to_ltl();
    if (unglue) {
        static const std::regex glued(R"((^|[^A-Za-z0-9_])([XFG])(iap_[A-Za-z0-9_]+))");
        ltl = std::regex_replace(ltl, glued, "$1$2($3)");
    }
    return ltl;
}

using fingerprint::LassoWord;

void add_signals(const Specification& spec, std::vector<std::string>& out) {
    for (const std::string& signal : environment_signals(spec)) {
        out.push_back(signal);
    }
    for (const std::string& signal : spec.m_out_atoms) {
        out.push_back(signal);
    }
}

std::vector<std::string> requirement_ltls(const Specification& spec) {
    std::vector<std::string> out;
    for (const auto* reqs : {&spec.m_assumptions, &spec.m_guarantees}) {
        for (const Requirement& req : *reqs) {
            if (!req.m_removed) {
                out.push_back(requirement_to_ltl(req));
            }
        }
    }
    return out;
}

LassoWord random_word(const std::vector<std::string>& signals,
                      std::size_t max_prefix, std::size_t max_cycle,
                      std::mt19937_64& rng) {
    std::uniform_int_distribution<std::size_t> prefix_dist(0, max_prefix);
    std::uniform_int_distribution<std::size_t> cycle_dist(1, max_cycle);
    const std::size_t prefix = prefix_dist(rng);
    const std::size_t cycle = cycle_dist(rng);
    LassoWord word;
    word.m_loop_start = prefix;
    word.m_n_positions = prefix + cycle;
    const std::uint64_t mask =
        word.m_n_positions == 64 ? ~std::uint64_t{0}
                                 : (std::uint64_t{1} << word.m_n_positions) - 1;
    for (const std::string& name : signals) {
        word.m_signal_masks[name] = rng() & mask;
    }
    return word;
}

// Satisfied-requirement count of each candidate word.
std::vector<std::size_t> scores(const std::vector<std::string>& reqs,
                                const std::vector<LassoWord>& words) {
    std::vector<std::size_t> out(words.size(), 0);
    for (const std::string& req : reqs) {
        const std::vector<bool> bits = fingerprint::fingerprint_of(req, words);
        for (std::size_t i = 0; i < words.size(); ++i) {
            out[i] += bits[i] ? 1 : 0;
        }
    }
    return out;
}

struct Drawn {
    std::optional<LassoWord> word;
    std::size_t steps = 0;
    std::size_t best = 0;
    std::size_t n_reqs = 0;
};

Drawn draw(const std::vector<Specification>& targets,
           const std::vector<std::string>& signals, std::size_t max_prefix,
           std::size_t max_cycle, std::size_t max_steps, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    const std::size_t target_index = rng() % targets.size();
    const Specification& target = targets[target_index];
    const std::vector<std::string> reqs = requirement_ltls(target);
    std::vector<std::vector<std::string>> req_atoms;
    for (const std::string& req : reqs) {
        std::vector<std::string> atoms;
        for (const std::string& name : signals) {
            const std::size_t at = req.find(name);
            if (at != std::string::npos) {
                atoms.push_back(name);
            }
        }
        req_atoms.push_back(std::move(atoms));
    }
    Drawn result;
    result.n_reqs = reqs.size();
    LassoWord current = random_word(signals, max_prefix, max_cycle, rng);
    std::size_t score = scores(reqs, {current})[0];
    result.best = score;
    for (std::size_t step = 0; step < max_steps; ++step) {
        if (score == reqs.size()) {
            const std::string whole = spelled(target);
            if (fingerprint::fingerprint_of(whole, {current})[0]) {
                result.word = current;
            }
            result.steps = step;
            return result;
        }
        // Focused move: one unsatisfied requirement, every flip of one of its
        // own atoms at one position.
        std::vector<std::size_t> unsat;
        for (std::size_t r = 0; r < reqs.size(); ++r) {
            if (!fingerprint::fingerprint_of(reqs[r], {current})[0]) {
                unsat.push_back(r);
            }
        }
        const std::vector<std::string>& atoms =
            req_atoms[unsat[rng() % unsat.size()]];
        std::vector<LassoWord> candidates;
        for (const std::string& name : atoms) {
            for (std::size_t position = 0; position < current.m_n_positions;
                 ++position) {
                LassoWord next = current;
                next.m_signal_masks[name] ^= std::uint64_t{1} << position;
                candidates.push_back(std::move(next));
            }
        }
        if (candidates.empty()) {
            break;
        }
        const std::vector<std::size_t> got = scores(reqs, candidates);
        std::size_t pick = 0;
        if (rng() % 10 == 0) {
            pick = rng() % candidates.size();
        } else {
            std::size_t best = 0;
            std::size_t ties = 0;
            for (std::size_t k = 0; k < got.size(); ++k) {
                if (got[k] > best) {
                    best = got[k];
                    pick = k;
                    ties = 1;
                } else if (got[k] == best && rng() % ++ties == 0) {
                    pick = k;
                }
            }
        }
        current = std::move(candidates[pick]);
        score = got[pick];
        result.best = std::max(result.best, score);
    }
    result.steps = max_steps;
    if (std::getenv("FPDRAW_DEBUG") != nullptr && seed % 1000003 < 3) {
        for (std::size_t r = 0; r < reqs.size(); ++r) {
            if (!fingerprint::fingerprint_of(reqs[r], {current})[0]) {
                std::cerr << "unsat [" << req_atoms[r].size() << " atoms]: "
                          << reqs[r] << '\n';
            }
        }
    }
    return result;
}


int fingerprint_all(const std::vector<std::string>& paths,
                    const std::vector<Specification>& specs,
                    const std::vector<LassoWord>& words,
                    std::size_t n_threads) {
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    std::vector<std::string> lines(specs.size());
    std::atomic<int> failures{0};
    for (std::size_t t = 0; t < n_threads; ++t) {
        pool.emplace_back([&] {
            for (std::size_t i = next++; i < specs.size(); i = next++) {
                try {
                    const fingerprint::PackedFingerprint packed =
                        fingerprint::pack(
                            fingerprint::fingerprint_of(spelled(specs[i]), words));
                    std::string hex;
                    hex.reserve(packed.size() * 16);
                    char buffer[17];
                    for (const std::uint64_t word : packed) {
                        std::snprintf(buffer, sizeof buffer, "%016llx",
                                      static_cast<unsigned long long>(word));
                        hex += buffer;
                    }
                    lines[i] = paths[i] + '\t' + hex + '\n';
                } catch (const std::exception& error) {
                    ++failures;
                }
            }
        });
    }
    for (std::thread& thread : pool) {
        thread.join();
    }
    for (const std::string& line : lines) {
        std::cout << line;
    }
    return failures == 0 ? 0 : 1;
}

// Each live requirement's LTL, joined by the unit separator.
std::string each(const std::vector<Requirement>& reqs) {
    std::string out;
    for (const Requirement& req : reqs) {
        if (!req.m_removed) {
            out += (out.empty() ? "" : "\x1f") + requirement_to_ltl(req);
        }
    }
    return out;
}

std::string conjunction(const std::vector<Requirement>& reqs) {
    std::string out;
    for (const Requirement& req : reqs) {
        if (req.m_removed) {
            continue;
        }
        out += (out.empty() ? "(" : " & (") + requirement_to_ltl(req) + ")";
    }
    return out.empty() ? "true" : out;
}

// ltl mode: "signals\t<sig> <sig>..." then per repair
// "<path>\t<assumptions>\t<guarantees>".
int print_ltl(const char* spec_path, const char* list_path) {
    std::vector<std::string> signals;
    add_signals(load_scored_specification(spec_path).spec, signals);
    std::vector<std::string> rows;
    std::ifstream list(list_path);
    for (std::string line; std::getline(list, line);) {
        if (line.empty()) {
            continue;
        }
        const Specification spec = load_scored_specification(line).spec;
        add_signals(spec, signals);
        rows.push_back(line + '\t' + conjunction(spec.m_assumptions) + '\t' +
                       conjunction(spec.m_guarantees) + '\t' +
                       each(spec.m_guarantees) + '\n');
    }
    std::sort(signals.begin(), signals.end());
    signals.erase(std::unique(signals.begin(), signals.end()), signals.end());
    std::cout << "signals\t";
    for (const std::string& signal : signals) {
        std::cout << signal << ' ';
    }
    std::cout << '\n';
    for (const std::string& row : rows) {
        std::cout << row;
    }
    return 0;
}

// eval mode: words file has one word a line, "<loop> <n> <sig>=<hexmask>...",
// and every listed repair is fingerprinted over those words.
int eval_words(const char* words_path, const char* list_path,
               std::size_t n_threads) {
    std::vector<LassoWord> words;
    std::ifstream in(words_path);
    for (std::string line; std::getline(in, line);) {
        std::istringstream fields(line);
        LassoWord word;
        fields >> word.m_loop_start >> word.m_n_positions;
        for (std::string item; fields >> item;) {
            const std::size_t eq = item.find('=');
            word.m_signal_masks[item.substr(0, eq)] =
                std::stoull(item.substr(eq + 1), nullptr, 16);
        }
        words.push_back(std::move(word));
    }
    std::vector<std::string> paths;
    std::vector<Specification> specs;
    std::ifstream list(list_path);
    for (std::string line; std::getline(list, line);) {
        if (!line.empty()) {
            specs.push_back(load_scored_specification(line).spec);
            paths.push_back(line);
        }
    }
    std::cerr << words.size() << " words, " << specs.size() << " repairs\n";
    return fingerprint_all(paths, specs, words, n_threads);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "ltl") {
        return print_ltl(argv[2], argv[3]);
    }
    if (argc == 5 && std::string(argv[1]) == "eval") {
        return eval_words(argv[2], argv[3], std::stoul(argv[4]));
    }
    if (argc != 9) {
        std::cerr << "usage: fpdraw words max_prefix max_cycle seed max_steps "
                     "threads spec list\n";
        return 2;
    }
    const std::size_t n_words = std::stoul(argv[1]);
    const std::size_t max_prefix = std::stoul(argv[2]);
    const std::size_t max_cycle = std::stoul(argv[3]);
    const std::uint64_t seed = std::stoull(argv[4]);
    const std::size_t max_steps = std::stoul(argv[5]);
    const std::size_t n_threads = std::stoul(argv[6]);
    std::vector<std::string> signals;
    add_signals(load_scored_specification(argv[7]).spec, signals);
    std::vector<std::string> paths;
    std::vector<Specification> specs;
    std::ifstream list(argv[8]);
    for (std::string line; std::getline(list, line);) {
        if (line.empty()) {
            continue;
        }
        Specification spec = load_scored_specification(line).spec;
        add_signals(spec, signals);
        paths.push_back(line);
        specs.push_back(std::move(spec));
    }
    std::sort(signals.begin(), signals.end());
    signals.erase(std::unique(signals.begin(), signals.end()), signals.end());

    std::vector<Drawn> drawn(n_words);
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    for (std::size_t t = 0; t < n_threads; ++t) {
        pool.emplace_back([&] {
            for (std::size_t i = next++; i < n_words; i = next++) {
                drawn[i] = draw(specs, signals, max_prefix, max_cycle,
                                max_steps, seed * 1000003 + i);
            }
        });
    }
    for (std::thread& thread : pool) {
        thread.join();
    }
    std::vector<LassoWord> words;
    std::size_t steps = 0;
    for (const Drawn& d : drawn) {
        steps += d.steps;
        if (d.word) {
            words.push_back(*d.word);
        }
    }
    std::vector<std::size_t> deficit(8, 0);
    for (const Drawn& d : drawn) {
        ++deficit[std::min<std::size_t>(d.n_reqs - d.best, 7)];
    }
    std::cerr << "deficit histogram (0..7+):";
    for (const std::size_t n : deficit) {
        std::cerr << ' ' << n;
    }
    std::cerr << '\n';
    std::cerr << "kept " << words.size() << " of " << n_words
              << " words, mean steps " << (n_words ? steps / n_words : 0)
              << '\n';
    if (words.empty()) {
        return 1;
    }

    return fingerprint_all(paths, specs, words, n_threads);
}
