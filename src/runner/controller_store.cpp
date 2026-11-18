#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "runner/formula_input.hpp"
#include "runner/process.hpp"
#include "runner/spot.hpp"
#include "runner/spot_internal.hpp"
#include "runner/tool_stats.hpp"

using spot_internal::includes_sorted;
using spot_internal::is_tautology_print_error;
using spot_internal::k_universal_hoa;
using spot_internal::signature_of;

namespace {

// Stored controllers kept per scope, and the candidates tried per query.
constexpr std::size_t k_controllers_per_scope = 64;
constexpr std::size_t k_reuse_candidates = 3;
// Products cached per controller, one per assumption set it was checked under.
constexpr std::size_t k_products_per_controller = 8;
// Automata cached for assumption sets and guarantee negations; past this the
// map is cleared rather than kept in order.
constexpr std::size_t k_automata_cache_cap = 4096;
// Above this many inputs the completeness walk over input valuations is not
// run, and the controller is not stored.
constexpr std::size_t k_max_controller_inputs = 16;
// Per-exec budget for the checks, cut to the ltlsynt budget when that is
// smaller: a check slower than the exec it replaces has lost already.
constexpr std::chrono::milliseconds k_reuse_exec_budget{2000};

// A directory of this process's own for the HOA files autfilt reads with
// --product, which takes a file and nothing else. Created on first use and
// removed with everything in it at exit. A run killed by a signal leaves it.
class ScratchDirectory {
   public:
    ScratchDirectory() {
        const char* tmpdir = std::getenv("TMPDIR");
        std::string base =
            tmpdir != nullptr && *tmpdir != '\0' ? tmpdir : "/tmp";
        while (base.size() > 1 && base.back() == '/') {
            base.pop_back();
        }
        std::string pattern = base + "/peredur-controllers-XXXXXX";
        if (mkdtemp(pattern.data()) != nullptr) {
            m_path = pattern;
        }
    }
    // remove_all reports through the error code; only an allocation can
    // throw, at exit, where there is nothing left to do about it.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    ~ScratchDirectory() {
        if (!m_path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }
    }
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;
    ScratchDirectory(ScratchDirectory&&) = delete;
    ScratchDirectory& operator=(ScratchDirectory&&) = delete;

    // Empty when the directory could not be made, which turns the store off.
    [[nodiscard]] const std::string& path() const { return m_path; }

   private:
    std::string m_path;
};

const std::string& scratch_directory() {
    static const ScratchDirectory k_directory;
    return k_directory.path();
}

// A new file name in the scratch directory, or empty when there is none.
std::string scratch_file(const char* stem) {
    static std::atomic<std::uint64_t> serial{0};
    const std::string& directory = scratch_directory();
    if (directory.empty()) {
        return {};
    }
    return directory + "/" + stem + std::to_string(serial++) + ".hoa";
}

bool write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    return static_cast<bool>(out);
}

std::chrono::milliseconds reuse_exec_budget() {
    const std::int64_t ltlsynt_ms = spot_internal::ltlsynt_timeout_ms();
    if (ltlsynt_ms > 0 && ltlsynt_ms < k_reuse_exec_budget.count()) {
        return std::chrono::milliseconds(ltlsynt_ms);
    }
    return k_reuse_exec_budget;
}

// The ids in both sorted vectors.
std::size_t shared_count(const std::vector<int>& lhs,
                         const std::vector<int>& rhs) {
    std::size_t count = 0;
    auto left = lhs.begin();
    auto right = rhs.begin();
    while (left != lhs.end() && right != rhs.end()) {
        if (*left < *right) {
            ++left;
        } else if (*right < *left) {
            ++right;
        } else {
            ++count;
            ++left;
            ++right;
        }
    }
    return count;
}

std::string ids_key(const std::vector<int>& ids) {
    std::string key;
    for (const int conjunct_id : ids) {
        key += std::to_string(conjunct_id);
        key += ',';
    }
    return key;
}

// The value after `name` on a header line of @p hoa, before @p body.
std::optional<std::string> hoa_header_value(const std::string& hoa,
                                            std::size_t body,
                                            const std::string& name) {
    const std::size_t line = hoa.find("\n" + name);
    if (line == std::string::npos || line > body) {
        return std::nullopt;
    }
    const std::size_t start = line + 1 + name.size();
    return hoa.substr(start, hoa.find('\n', start) - start);
}

// Each AP's bit in an input valuation, or -1 for an output, from the AP and
// controllable-AP headers. Empty when the header is not one this reads.
std::vector<int> mealy_input_bits(const std::string& hoa, std::size_t body) {
    const std::optional<std::string> aps = hoa_header_value(hoa, body, "AP:");
    if (!aps.has_value()) {
        return {};
    }
    const std::size_t n_aps = std::stoul(*aps);
    std::vector<bool> controllable(n_aps, false);
    const std::optional<std::string> outputs =
        hoa_header_value(hoa, body, "controllable-AP:");
    const std::string listed = outputs.value_or("");
    std::size_t cursor = listed.find_first_not_of(' ');
    while (cursor != std::string::npos) {
        std::size_t used = 0;
        const std::size_t index = std::stoul(listed.substr(cursor), &used);
        if (index >= n_aps) {
            return {};
        }
        controllable[index] = true;
        cursor = listed.find_first_not_of(' ', cursor + used);
    }
    std::vector<int> input_bit(n_aps, -1);
    int n_inputs = 0;
    for (std::size_t index = 0; index < n_aps; ++index) {
        if (!controllable[index]) {
            input_bit[index] = n_inputs++;
        }
    }
    return input_bit;
}

// One cube's input literals: the bits it fixes and their values.
struct InputCube {
    std::uint32_t m_mask = 0;
    std::uint32_t m_value = 0;
};

// Adds one literal to @p cube when its AP is an input, or false when the cube
// already fixes that input the other way.
bool add_literal(InputCube& cube, int input_bit, bool negated) {
    if (input_bit < 0) {
        return true;
    }
    const std::uint32_t bit = std::uint32_t{1}
                              << static_cast<unsigned>(input_bit);
    const std::uint32_t value = negated ? 0U : bit;
    const bool consistent =
        (cube.m_mask & bit) == 0 || (cube.m_value & bit) == value;
    cube.m_mask |= bit;
    cube.m_value |= value;
    return consistent;
}

// The input projection of each cube in the label of an edge line, appended to
// @p cubes. A cube contradicting itself on an input enables nothing and is
// dropped. False on any label this does not read.
bool read_label_cubes(const std::string& line,
                      const std::vector<int>& input_bit,
                      std::vector<InputCube>& cubes) {
    const std::size_t close = line.find(']');
    if (line.empty() || line[0] != '[' || close == std::string::npos) {
        return false;
    }
    InputCube cube;
    bool contradictory = false;
    bool negated = false;
    for (std::size_t cursor = 1; cursor <= close; ++cursor) {
        const char symbol = line[cursor];
        if (symbol == '|' || symbol == ']') {
            if (!contradictory) {
                cubes.push_back(cube);
            }
            cube = InputCube{};
            contradictory = false;
        } else if (symbol == '!') {
            negated = true;
            continue;
        } else if (symbol == 'f') {
            contradictory = true;
        } else if (symbol >= '0' && symbol <= '9') {
            std::size_t used = 0;
            const std::size_t index = std::stoul(line.substr(cursor), &used);
            cursor += used - 1;
            if (index >= input_bit.size()) {
                return false;
            }
            contradictory =
                !add_literal(cube, input_bit[index], negated) || contradictory;
        } else if (symbol != ' ' && symbol != '&' && symbol != 't') {
            return false;
        }
        negated = false;
    }
    return true;
}

// Whether the cubes leaving one state enable every valuation of the inputs.
bool enables_every_input(const std::vector<InputCube>& cubes,
                         std::size_t n_inputs) {
    const std::uint32_t n_valuations = std::uint32_t{1} << n_inputs;
    for (std::uint32_t valuation = 0; valuation < n_valuations; ++valuation) {
        const bool enabled =
            std::any_of(cubes.begin(), cubes.end(), [&](const InputCube& cube) {
                return (valuation & cube.m_mask) == cube.m_value;
            });
        if (!enabled) {
            return false;
        }
    }
    return true;
}

// Whether @p hoa is a Mealy machine the store can believe: one accepting
// condition over every run, and a transition for every input valuation from
// every state. The second is what the soundness of the check rests on. A
// machine missing an input at some state drops every word through it from its
// language, and a product emptiness check would then pass a guarantee those
// words violate. ltlsynt emitted only complete machines on all 2,097
// realizable queries of the recorded lift-plus-cruise-full stream, so this is
// a guard rather than a filter.
//
// Labels are read as SPOT prints them, a disjunction of conjunctions of
// literals over AP indices. Anything else is refused, not interpreted.
bool is_input_complete_mealy(const std::string& hoa) {
    const std::size_t body = hoa.find("--BODY--");
    if (body == std::string::npos ||
        hoa_header_value(hoa, body, "Acceptance:") != " 0 t") {
        return false;
    }
    const std::vector<int> input_bit = mealy_input_bits(hoa, body);
    const auto n_inputs = static_cast<std::size_t>(std::count_if(
        input_bit.begin(), input_bit.end(), [](int bit) { return bit >= 0; }));
    if (input_bit.empty() || n_inputs > k_max_controller_inputs) {
        return false;
    }
    std::vector<std::vector<InputCube>> states;
    std::size_t line_start = hoa.find('\n', body);
    while (line_start != std::string::npos && line_start + 1 < hoa.size()) {
        const std::size_t line_end = hoa.find('\n', line_start + 1);
        const std::string line =
            hoa.substr(line_start + 1, line_end == std::string::npos
                                           ? std::string::npos
                                           : line_end - line_start - 1);
        line_start = line_end;
        if (line.rfind("--END--", 0) == 0) {
            break;
        }
        if (line.rfind("State:", 0) == 0) {
            states.emplace_back();
        } else if (states.empty() ||
                   !read_label_cubes(line, input_bit, states.back())) {
            return false;
        }
    }
    return !states.empty() &&
           std::all_of(states.begin(), states.end(),
                       [n_inputs](const std::vector<InputCube>& cubes) {
                           return enables_every_input(cubes, n_inputs);
                       });
}
}  // namespace

namespace spot_internal {

bool controller_reuse_available() {
    static const bool k_available = !scratch_directory().empty() &&
                                    access(autfilt_path().c_str(), X_OK) == 0;
    return k_available;
}

}  // namespace spot_internal

// One stored Mealy machine, with the sides of the query it was synthesised
// for. The file goes with the last reference, so a check still reading it
// outlives an eviction.
struct RealizabilityChecker::StoredController {
    std::vector<int> m_assumptions;
    std::vector<int> m_guarantees;
    std::string m_path;
    /// Products by assumption-set key, under m_reuse_mutex.
    std::unordered_map<std::string, std::shared_ptr<ControllerProduct>>
        m_products;

    StoredController() = default;
    StoredController(const StoredController&) = delete;
    StoredController& operator=(const StoredController&) = delete;
    StoredController(StoredController&&) = delete;
    StoredController& operator=(StoredController&&) = delete;
    ~StoredController() { std::remove(m_path.c_str()); }
};

// A controller's product with the automaton of one assumption set, and the
// guarantee checks already run against it.
struct RealizabilityChecker::ControllerProduct {
    std::string m_path;
    /// False when m_path is the controller's own file, as it is under no
    /// assumptions.
    bool m_owns_file = true;
    /// Guarantee id to whether the product with its negation was empty, under
    /// m_reuse_mutex.
    std::unordered_map<int, bool> m_verdicts;

    ControllerProduct() = default;
    ControllerProduct(const ControllerProduct&) = delete;
    ControllerProduct& operator=(const ControllerProduct&) = delete;
    ControllerProduct(ControllerProduct&&) = delete;
    ControllerProduct& operator=(ControllerProduct&&) = delete;
    ~ControllerProduct() {
        if (m_owns_file) {
            std::remove(m_path.c_str());
        }
    }
};

std::optional<std::string> RealizabilityChecker::assumption_automaton(
    const ReuseQuery& query) {
    const std::string key = ids_key(query.m_assumptions);
    {
        std::scoped_lock lock(m_reuse_mutex);
        const auto found = m_assumption_automata.find(key);
        if (found != m_assumption_automata.end()) {
            return found->second;
        }
    }
    std::string conjunction;
    for (const std::string& formula : query.m_assumption_formulas) {
        conjunction += conjunction.empty() ? "(" : " & (";
        conjunction += formula;
        conjunction += ")";
    }
    const ProcessResult result = execute_and_capture_with_input(
        {ltl2tgba_path(), "-H", "-F", "-"}, spot_formula_line(conjunction),
        reuse_exec_budget());
    std::optional<std::string> automaton;
    if (!result.m_timed_out && is_tautology_print_error(result)) {
        automaton = k_universal_hoa;
    } else if (!result.m_timed_out && result.m_exit_code == 0 &&
               result.m_output.rfind("HOA:", 0) == 0) {
        automaton = result.m_output;
    }
    std::scoped_lock lock(m_reuse_mutex);
    record_exec<ControllerReuseStats>(result);
    ControllerReuseStats::n_execs++;
    // A failure, a timeout among them, is memoised as nullopt: ltl2tgba is
    // deterministic, and every query it stops falls through to ltlsynt.
    if (m_assumption_automata.size() >= k_automata_cache_cap) {
        m_assumption_automata.clear();
    }
    m_assumption_automata.emplace(key, automaton);
    return automaton;
}

std::optional<std::string> RealizabilityChecker::negation_automaton(
    const std::string& formula) {
    {
        std::scoped_lock lock(m_reuse_mutex);
        const auto found = m_negation_automata.find(formula);
        if (found != m_negation_automata.end()) {
            return found->second;
        }
    }
    const ProcessResult result = execute_and_capture_with_input(
        {ltl2tgba_path(), "-H", "-F", "-"},
        spot_formula_line("!(" + formula + ")"), reuse_exec_budget());
    // A tautologous negation is a guarantee no controller meets, and is left
    // to ltlsynt like any other failure.
    std::optional<std::string> automaton;
    if (!result.m_timed_out && result.m_exit_code == 0 &&
        result.m_output.rfind("HOA:", 0) == 0) {
        automaton = result.m_output;
    }
    std::scoped_lock lock(m_reuse_mutex);
    record_exec<ControllerReuseStats>(result);
    ControllerReuseStats::n_execs++;
    if (m_negation_automata.size() >= k_automata_cache_cap) {
        m_negation_automata.clear();
    }
    m_negation_automata.emplace(formula, automaton);
    return automaton;
}

std::shared_ptr<RealizabilityChecker::ControllerProduct>
RealizabilityChecker::product_for(
    const std::shared_ptr<StoredController>& controller,
    const ReuseQuery& query) {
    const std::string key = ids_key(query.m_assumptions);
    {
        std::scoped_lock lock(m_reuse_mutex);
        const auto found = controller->m_products.find(key);
        if (found != controller->m_products.end()) {
            return found->second;
        }
    }
    auto product = std::make_shared<ControllerProduct>();
    if (query.m_assumptions.empty()) {
        product->m_path = controller->m_path;
        product->m_owns_file = false;
    } else {
        const std::optional<std::string> assumptions =
            assumption_automaton(query);
        if (!assumptions.has_value()) {
            return nullptr;
        }
        const ProcessResult result = execute_and_capture_with_input(
            {autfilt_path(), "--product=" + controller->m_path, "-"},
            *assumptions, reuse_exec_budget());
        {
            std::scoped_lock lock(m_reuse_mutex);
            record_exec<ControllerReuseStats>(result);
            ControllerReuseStats::n_execs++;
        }
        if (result.m_timed_out || result.m_exit_code != 0 ||
            result.m_output.rfind("HOA:", 0) != 0) {
            return nullptr;
        }
        product->m_path = scratch_file("product-");
        if (product->m_path.empty() ||
            !write_file(product->m_path, result.m_output)) {
            return nullptr;
        }
    }
    std::scoped_lock lock(m_reuse_mutex);
    if (controller->m_products.size() >= k_products_per_controller) {
        controller->m_products.clear();
    }
    // A concurrent check may have built the same product first; either is
    // the same automaton, and the one kept carries any verdicts already in.
    return controller->m_products.emplace(key, product).first->second;
}

std::vector<std::shared_ptr<RealizabilityChecker::StoredController>>
RealizabilityChecker::reuse_candidates(const ReuseQuery& query) {
    std::scoped_lock lock(m_reuse_mutex);
    const auto bucket = m_controllers.find(query.m_scope);
    if (bucket == m_controllers.end()) {
        return {};
    }
    // The k entries sharing the most conjuncts, newest first among equals.
    const auto& stored = bucket->second;
    std::vector<std::pair<std::size_t, std::size_t>> ranked;
    ranked.reserve(stored.size());
    for (std::size_t age = 0; age < stored.size(); ++age) {
        const StoredController& entry = *stored[stored.size() - 1 - age];
        ranked.emplace_back(
            shared_count(entry.m_assumptions, query.m_assumptions) +
                shared_count(entry.m_guarantees, query.m_guarantees),
            age);
    }
    const std::size_t keep = std::min(k_reuse_candidates, ranked.size());
    std::partial_sort(
        ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(keep),
        ranked.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first != rhs.first ? lhs.first > rhs.first
                                          : lhs.second < rhs.second;
        });
    std::vector<std::shared_ptr<StoredController>> candidates;
    candidates.reserve(keep);
    for (std::size_t rank = 0; rank < keep; ++rank) {
        candidates.push_back(stored[stored.size() - 1 - ranked[rank].second]);
    }
    if (!candidates.empty()) {
        ControllerReuseStats::n_attempts++;
    }
    return candidates;
}

bool RealizabilityChecker::product_meets(ControllerProduct& product,
                                         int guarantee,
                                         const std::string& formula) {
    {
        std::scoped_lock lock(m_reuse_mutex);
        const auto found = product.m_verdicts.find(guarantee);
        if (found != product.m_verdicts.end()) {
            ControllerReuseStats::n_verdict_hits++;
            return found->second;
        }
    }
    const std::optional<std::string> negation = negation_automaton(formula);
    if (!negation.has_value()) {
        return false;
    }
    // --stats prints one line per automaton autfilt outputs, which is the
    // product; %w is a word it accepts, and nothing when it is empty.
    // --is-empty would not do: it filters the input, before the product is
    // taken. The brackets make an empty word a positive answer, so that no
    // output at all is never read as a pass.
    const ProcessResult result = execute_and_capture_with_input(
        {autfilt_path(), "--product=" + product.m_path, "--stats=<%w>", "-"},
        *negation, reuse_exec_budget());
    const bool decided = !result.m_timed_out && result.m_exit_code == 0 &&
                         result.m_output.rfind('<', 0) == 0;
    const bool empty = decided && result.m_output == "<>\n";
    std::scoped_lock lock(m_reuse_mutex);
    record_exec<ControllerReuseStats>(result);
    ControllerReuseStats::n_execs++;
    ControllerReuseStats::n_guarantees_checked++;
    if (decided) {
        product.m_verdicts.emplace(guarantee, empty);
    }
    return empty;
}

bool RealizabilityChecker::controller_realizes(const ReuseQuery& query) {
    for (const auto& controller : reuse_candidates(query)) {
        // The controller realizes (&A_e) -> (&G_e), so on every word it
        // produces each g in G_e holds once A_e does. Where the query assumes
        // at least A_e those guarantees hold under its assumptions too, and
        // only the ones the entry lacked need checking; otherwise all do. The
        // ones it lacked go first, being the likelier to fail.
        std::vector<std::size_t> order;
        std::vector<std::size_t> shared;
        for (std::size_t position = 0; position < query.m_guarantees.size();
             ++position) {
            const bool known = std::binary_search(
                controller->m_guarantees.begin(),
                controller->m_guarantees.end(), query.m_guarantees[position]);
            (known ? shared : order).push_back(position);
        }
        if (!includes_sorted(query.m_assumptions, controller->m_assumptions)) {
            order.insert(order.end(), shared.begin(), shared.end());
        }
        const std::shared_ptr<ControllerProduct> product =
            product_for(controller, query);
        if (product != nullptr &&
            std::all_of(order.begin(), order.end(), [&](std::size_t position) {
                return product_meets(*product, query.m_guarantees[position],
                                     query.m_guarantee_formulas[position]);
            })) {
            std::scoped_lock lock(m_reuse_mutex);
            ControllerReuseStats::n_hits++;
            return true;
        }
    }
    return false;
}

void RealizabilityChecker::store_controller(const ReuseQuery& query,
                                            const std::string& hoa) {
    bool complete = false;
    try {
        complete = is_input_complete_mealy(hoa);
    } catch (const std::exception&) {
        // An index past what stoul reads is a machine not stored, as any
        // other shape the reader does not know is.
        return;
    }
    if (!complete) {
        return;
    }
    auto controller = std::make_shared<StoredController>();
    controller->m_assumptions = query.m_assumptions;
    controller->m_guarantees = query.m_guarantees;
    controller->m_path = scratch_file("controller-");
    if (controller->m_path.empty() || !write_file(controller->m_path, hoa)) {
        return;
    }
    std::scoped_lock lock(m_reuse_mutex);
    auto& bucket = m_controllers[query.m_scope];
    bucket.push_back(std::move(controller));
    if (bucket.size() > k_controllers_per_scope) {
        bucket.pop_front();
    }
    ControllerReuseStats::n_stored++;
}

bool RealizabilityChecker::settled_by_controller(const ReuseQuery& query,
                                                 const std::string& cache_key) {
    const bool realized = controller_realizes(query);
    std::scoped_lock lock(m_cache_mutex);
    if (!realized) {
        n_cache_misses++;
        return false;
    }
    // Recorded as a decided verdict, since it is one. Its controller is not
    // stored: the one that answered it already is.
    m_cache.emplace(cache_key, true);
    m_decided[query.m_scope].push_back(
        Decided{query.m_assumptions, query.m_guarantees,
                signature_of(query.m_assumptions),
                signature_of(query.m_guarantees), true});
    return true;
}

RealizabilityChecker::ReuseQuery RealizabilityChecker::reuse_query(
    const std::string& scope, const std::vector<int>& assumptions,
    const std::vector<int>& guarantees) const {
    ReuseQuery query{scope, assumptions, guarantees, {}, {}};
    for (const int conjunct_id : assumptions) {
        query.m_assumption_formulas.push_back(m_conjunct_formulas[conjunct_id]);
    }
    for (const int conjunct_id : guarantees) {
        query.m_guarantee_formulas.push_back(m_conjunct_formulas[conjunct_id]);
    }
    return query;
}
