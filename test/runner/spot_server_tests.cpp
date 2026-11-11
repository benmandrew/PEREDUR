#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "posix/descriptors.hpp"
#include "runner/process.hpp"
#include "runner/spot.hpp"
#include "runner/tool_paths.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "spot_server_runner";

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr milliseconds k_budget{60'000};

// A build without a server (macOS, for now) has nothing here to test.
bool have_server() { return !spot_server_path_source().m_path.empty(); }

std::string tool(const std::string& name) {
    return spot_bin_dir() + "/" + name;
}

// The server on for one test. Off again afterwards, so every later suite in
// the process runs its tools as execs, as it always has.
class ServerOn {
   public:
    ServerOn() {
        expect(configure_spot_server(true),
               "spot server: configure_spot_server(true) turns it on");
    }
    ~ServerOn() { configure_spot_server(false); }
    ServerOn(const ServerOn&) = delete;
    ServerOn& operator=(const ServerOn&) = delete;
    ServerOn(ServerOn&&) = delete;
    ServerOn& operator=(ServerOn&&) = delete;
};

ProcessResult served(const std::vector<std::string>& arguments,
                     const std::string& input,
                     milliseconds timeout = k_budget) {
    return execute_and_capture_with_input(arguments, input, timeout);
}

// The exec of the same call, with the server still on. SearchPath is never
// served, and execvp of a path containing a slash execs that path as execv
// does.
ProcessResult execed(const std::vector<std::string>& arguments,
                     const std::string& input) {
    return execute_and_capture_with_input(arguments, input, k_budget,
                                          ExecutableLookup::SearchPath);
}

// F(a & X^24 b) as a deterministic automaton, about 2^24 states: minutes.
std::string slow_formula() {
    std::string formula = "F(a & ";
    for (int step = 0; step < 24; ++step) {
        formula += "X ";
    }
    return formula + "b)";
}

std::vector<std::string> slow_arguments() {
    return {tool("ltl2tgba"), "-D", "-S", "-H", "-F", "-"};
}

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
}

// Whether the process group led by `leader` has emptied within five seconds.
bool group_gone(pid_t leader) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (killpg(leader, 0) != 0 && errno == ESRCH) {
            return true;
        }
        std::this_thread::sleep_for(milliseconds{10});
    }
    killpg(leader, SIGKILL);
    return false;
}

// Reaps `pid` within five seconds, returning its wait status, or -1.
int reap_within(pid_t pid) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            return status;
        }
        std::this_thread::sleep_for(milliseconds{10});
    }
    killpg(pid, SIGKILL);
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    return -1;
}

struct Query {
    std::vector<std::string> m_arguments;
    std::string m_input;
};

// The checked-in corpus, sampled from three logged runs on the small FRETISH
// specifications: 40 calls of each of the four kinds those runs made most, the
// two `--hoaf` calls they made (both unrealizable), and last, eight realizable
// `--realizability` calls asked again with `--hoaf`, so that controllers are
// stored. Each argv[0] is stored as the tool's name.
std::vector<Query> corpus() {
    std::ifstream file(std::string(PEREDUR_TEST_DATA_DIR) +
                       "/spot_server_corpus.jsonl");
    expect(file.good(), "spot server: the corpus is readable");
    std::vector<Query> queries;
    std::string line;
    while (std::getline(file, line)) {
        const nlohmann::json record = nlohmann::json::parse(line);
        Query query;
        query.m_arguments = record.at("argv").get<std::vector<std::string>>();
        query.m_arguments[0] = tool(query.m_arguments[0]);
        query.m_input = record.at("input").get<std::string>();
        queries.push_back(std::move(query));
    }
    return queries;
}

bool is_hoaf(const Query& query) {
    return std::find(query.m_arguments.begin(), query.m_arguments.end(),
                     "--hoaf") != query.m_arguments.end();
}

std::string first_line(const std::string& text) {
    return text.substr(0, text.find('\n'));
}

TEST(test_server_answers_each_tool_as_its_exec) {
    if (!have_server()) {
        return;
    }
    const ServerOn server;
    const TempDir dir("spot_server_tools");
    const ProcessResult automaton =
        execed({tool("ltl2tgba"), "-H", "-F", "-"}, "G a\n");
    const std::string product_file =
        write_text(dir.path() / "ga.hoa", automaton.m_output).string();
    const std::vector<Query> calls = {
        {{tool("ltlsynt"), "--ins=a", "--outs=b", "--realizability", "-F", "-"},
         "G(a <-> X b)\n"},
        {{tool("ltlfilt"), "--satisfiable", "-F", "-"}, "a & F b\n"},
        // Exits 1 with no output: the exit code has to come back too.
        {{tool("ltlfilt"), "--satisfiable", "-F", "-"}, "a & !a\n"},
        {{tool("ltl2tgba"), "-D", "-S", "-H", "-F", "-"}, "GF a & GF b\n"},
        {{tool("autfilt"), "--product=" + product_file, "-"},
         execed({tool("ltl2tgba"), "-H", "-F", "-"}, "F b\n").m_output},
    };
    const std::size_t requests_before = ToolServerStats::n_requests.load();
    const std::uint64_t cpu_ns_before = ToolServerStats::n_cpu_ns.load();
    double served_cpu_s = 0.0;
    for (const Query& call : calls) {
        const ProcessResult expected = execed(call.m_arguments, call.m_input);
        const ProcessResult got = served(call.m_arguments, call.m_input);
        const std::string label =
            "spot server: " + call.m_arguments[0] + " " + call.m_arguments[1];
        expect(got.m_exit_code == expected.m_exit_code,
               label + " exits " + std::to_string(expected.m_exit_code) +
                   " as its exec does, got " + std::to_string(got.m_exit_code));
        expect(got.m_output == expected.m_output,
               label + " prints what its exec prints");
        expect(!got.m_timed_out, label + " does not time out");
        served_cpu_s += got.m_cpu_s;
        if (call.m_input == "a & !a\n") {
            expect(got.m_exit_code == 1 && got.m_output.empty(),
                   "spot server: an unsatisfiable formula exits 1 with no "
                   "output through the server");
        }
    }
    expect(ToolServerStats::n_requests.load() == requests_before + calls.size(),
           "spot server: every call went through the server");
    expect(served_cpu_s > 0.0,
           "spot server: the tool child's CPU time reaches ProcessResult");
    const double counted_s =
        static_cast<double>(ToolServerStats::n_cpu_ns.load() - cpu_ns_before) /
        1e9;
    expect(std::abs(counted_s - served_cpu_s) < 1e-6,
           "spot server: the run's server CPU total is the sum of the replies' "
           "CPU, for the CPU report");
}

TEST(test_server_timeout_kills_the_tool_and_keeps_the_server) {
    if (!have_server()) {
        return;
    }
    const ServerOn server;
    const std::size_t started_before = ToolServerStats::n_started.load();
    const std::size_t killed_before = ToolServerStats::n_killed.load();
    const std::size_t discarded_before = ToolServerStats::n_discarded.load();
    const auto start = Clock::now();
    const ProcessResult result =
        served(slow_arguments(), slow_formula() + "\n", milliseconds{300});
    expect(result.m_timed_out, "spot server: a slow call times out");
    expect(result.m_exit_code == 128 + SIGKILL,
           "spot server: a timed-out call reports the kill");
    expect(elapsed_ms(start) < 10'000.0,
           "spot server: the timeout fires on time");
    expect(result.m_cpu_s > 0.0,
           "spot server: a killed tool's CPU reaches ProcessResult, as an "
           "exec's reap gives it");
    expect(ToolServerStats::n_killed.load() == killed_before + 1 &&
               ToolServerStats::n_discarded.load() == discarded_before,
           "spot server: the server kills its tool and is kept");

    const ProcessResult after =
        served({tool("ltlfilt"), "-F", "-"}, "a\n", k_budget);
    expect(after.m_exit_code == 0 && after.m_output == "a\n",
           "spot server: the call after a timeout is answered");
    expect(ToolServerStats::n_started.load() == started_before + 1,
           "spot server: the server that killed its tool answers the next "
           "call");
}

// A server that cannot kill its tool, here one whose tool ignores the kill
// for longer than the grace, goes with its whole group instead. A tool child
// cannot ignore SIGKILL, so the stand-in is a server that never replies.
TEST(test_server_unanswered_kill_discards_the_server) {
    if (!have_server()) {
        return;
    }
    const ServerOn server;
    const TempDir dir("spot_server_deaf");
    const std::filesystem::path script =
        write_text(dir.path() / "deaf-server",
                   "#!/bin/sh\nprintf 'ready\\n'\nexec sleep 60\n");
    std::filesystem::permissions(script, std::filesystem::perms::owner_all);
    set_tool_server({script.string(), {tool("ltlfilt")}});
    const std::size_t discarded_before = ToolServerStats::n_discarded.load();
    const auto start = Clock::now();
    const ProcessResult result =
        served({tool("ltlfilt"), "-F", "-"}, "a\n", milliseconds{200});
    expect(result.m_timed_out && result.m_exit_code == 128 + SIGKILL,
           "spot server: a call to a server that never replies times out");
    expect(elapsed_ms(start) < 5'000.0,
           "spot server: an unanswered kill costs at most its grace");
    expect(ToolServerStats::n_discarded.load() == discarded_before + 1,
           "spot server: a server that does not answer its kill is "
           "discarded");
}

// A new server's start-up is not part of the call that needs it, or a call's
// timeout would depend on whether an earlier one had discarded a server. The
// server here takes 500 ms to start and the call has 300 ms.
TEST(test_server_start_up_is_not_charged_to_the_call) {
    if (!have_server()) {
        return;
    }
    const ServerOn server;
    const TempDir dir("spot_server_slow_start");
    const std::filesystem::path script =
        write_text(dir.path() / "slow-server",
                   "#!/bin/sh\nsleep 0.5\nexec '" +
                       spot_server_path_source().m_path + "'\n");
    std::filesystem::permissions(script, std::filesystem::perms::owner_all);
    set_tool_server({script.string(), {tool("ltlfilt")}});
    const ProcessResult result =
        served({tool("ltlfilt"), "-F", "-"}, "a\n", milliseconds{300});
    expect(!result.m_timed_out && result.m_exit_code == 0 &&
               result.m_output == "a\n",
           "spot server: a call is answered within its budget however long "
           "the server took to start");
    expect(result.m_wall_s < 0.3,
           "spot server: the call's wall time leaves out the start-up");
}

TEST(test_server_cancel_kills_the_tool_and_raises) {
    if (!have_server()) {
        return;
    }
    const ServerOn server;
    const std::size_t killed_before = ToolServerStats::n_killed.load();
    std::atomic<bool> cancel{false};
    bool raised = false;
    const auto start = Clock::now();
    {
        const ExecCancelScope scope(cancel);
        std::thread canceller([&cancel] {
            std::this_thread::sleep_for(milliseconds{200});
            cancel.store(true);
        });
        try {
            (void)served(slow_arguments(), slow_formula() + "\n");
        } catch (const ExecCancelled&) {
            raised = true;
        }
        canceller.join();
        bool refused = false;
        try {
            (void)served({tool("ltlfilt"), "-F", "-"}, "a\n");
        } catch (const ExecCancelled&) {
            refused = true;
        }
        expect(refused,
               "spot server: a flag set before the call starts nothing");
    }
    expect(raised, "spot server: a cancelled call raises ExecCancelled");
    expect(ToolServerStats::n_killed.load() == killed_before + 1,
           "spot server: a cancellation kills the tool and keeps the server");
    expect(elapsed_ms(start) < 5'000.0,
           "spot server: cancellation kills the tool promptly");
    const ProcessResult after = served({tool("ltlfilt"), "-F", "-"}, "a\n");
    expect(after.m_exit_code == 0 && after.m_output == "a\n",
           "spot server: the call after a cancellation is answered");
}

// End of file on the server's stdin is how it learns the client has gone,
// whether it is idle or running a tool.
TEST(test_server_exits_on_end_of_input) {
    if (!have_server()) {
        return;
    }
    const std::string path = spot_server_path_source().m_path;
    const PipedChild idle =
        spawn_piped_child({path}, ParentDeathPolicy::SurviveParentThread,
                          ExecutableLookup::AbsolutePath);
    close(idle.m_write_fd);
    const int idle_status = reap_within(idle.m_pid);
    close(idle.m_read_fd);
    expect(idle_status >= 0 && WIFEXITED(idle_status) &&
               WEXITSTATUS(idle_status) == 0,
           "spot server: an idle server exits 0 at end of input");
    expect(group_gone(idle.m_pid),
           "spot server: an idle server leaves no zygote behind");

    const PipedChild busy =
        spawn_piped_child({path}, ParentDeathPolicy::SurviveParentThread,
                          ExecutableLookup::AbsolutePath);
    const std::string formula = slow_formula();
    const std::string request = "6\n" + tool("ltl2tgba") +
                                "\n-D\n-S\n-H\n-F\n-\n" +
                                std::to_string(formula.size()) + "\n" + formula;
    expect(write_all(busy.m_write_fd, request.data(), request.size()),
           "spot server: the slow request is written");
    std::this_thread::sleep_for(milliseconds{300});
    close(busy.m_write_fd);
    const int busy_status = reap_within(busy.m_pid);
    close(busy.m_read_fd);
    expect(busy_status >= 0,
           "spot server: a server running a tool exits at end of input");
    expect(group_gone(busy.m_pid),
           "spot server: the running tool dies with its server");
}

// SIGKILL leaves peredur no teardown. The kernel closing the server's stdin
// is all the server sees, and it has to take the zygote and the tool with it.
TEST(test_a_killed_client_leaves_no_server_or_tool) {
    if (!have_server()) {
        return;
    }
    const PipedChild helper = spawn_piped_child(
        {PEREDUR_CONTAINED_CHILD_HELPER, "--server",
         spot_server_path_source().m_path, tool("ltl2tgba")},
        ParentDeathPolicy::SurviveParentThread, ExecutableLookup::AbsolutePath);
    const std::pair<std::string, bool> report =
        read_until_eof(helper.m_read_fd, milliseconds{10'000});
    close(helper.m_write_fd);
    close(helper.m_read_fd);
    if (report.second || report.first.empty()) {
        killpg(helper.m_pid, SIGKILL);
        waitpid(helper.m_pid, nullptr, 0);
        fail("spot server: the helper never reported its server's pid");
    }
    const auto server_pid = static_cast<pid_t>(std::stoi(report.first));
    // Long enough for the zygote to have forked the tool.
    std::this_thread::sleep_for(milliseconds{300});
    expect(killpg(server_pid, 0) == 0,
           "spot server: the helper's server is running");
    kill(helper.m_pid, SIGKILL);
    waitpid(helper.m_pid, nullptr, 0);
    expect(group_gone(server_pid),
           "spot server: SIGKILL of its client left the server, its zygote "
           "or its tool running");
}

// The checked-in corpus through both paths. Byte-identical answers, except
// for ltlsynt's controllers, whose choice among valid ones follows the heap
// layout (see "Tool subprocesses" in docs/dev/performance.md): those have to
// agree on the verdict only.
TEST(test_server_replays_the_corpus_as_its_execs) {
    if (!have_server()) {
        return;
    }
    const ServerOn server;
    const TempDir dir("spot_server_corpus");
    std::size_t controllers = 0;
    for (const Query& query : corpus()) {
        const ProcessResult expected = execed(query.m_arguments, query.m_input);
        const ProcessResult got = served(query.m_arguments, query.m_input);
        const std::string label = "spot server: corpus call " +
                                  query.m_arguments[0] + " " +
                                  query.m_arguments[1];
        expect(got.m_exit_code == expected.m_exit_code,
               label + " exits as its exec does");
        if (!is_hoaf(query)) {
            expect(got.m_output == expected.m_output,
                   label + " prints what its exec prints");
            continue;
        }
        expect(first_line(got.m_output) == first_line(expected.m_output),
               label + " gives its exec's verdict");
        // The controller store's product: autfilt --product= over a stored
        // controller, the call a one-definition clash once broke in every
        // autfilt child.
        const std::size_t strategy = expected.m_output.find("HOA:");
        if (strategy == std::string::npos) {
            continue;
        }
        const std::string controller = expected.m_output.substr(strategy);
        const std::string stored =
            write_text(dir.path() / ("controller-" +
                                     std::to_string(controllers++) + ".hoa"),
                       controller)
                .string();
        for (const std::vector<std::string>& product :
             {std::vector<std::string>{tool("autfilt"), "--product=" + stored,
                                       "-"},
              std::vector<std::string>{tool("autfilt"), "--product=" + stored,
                                       "--stats=<%w>", "-"}}) {
            const ProcessResult product_exec = execed(product, controller);
            const ProcessResult product_served = served(product, controller);
            expect(product_served.m_exit_code == product_exec.m_exit_code &&
                       product_served.m_output == product_exec.m_output,
                   "spot server: autfilt " + product[2] +
                       " over a stored controller prints what its exec "
                       "prints");
        }
    }
    expect(controllers > 0,
           "spot server: the corpus stores at least one controller");
}

// Each tool child starts from the zygote's heap, whatever the server ran
// before, so a controller is a function of its request alone.
TEST(test_server_answer_does_not_depend_on_history) {
    if (!have_server()) {
        return;
    }
    const std::vector<Query> queries = corpus();
    // The last call is a realizable --hoaf one, so its answer is a controller.
    const Query* probe = queries.empty() ? nullptr : &queries.back();
    if (probe != nullptr && !is_hoaf(*probe)) {
        probe = nullptr;
    }
    expect(probe != nullptr, "spot server: the corpus holds a --hoaf call");
    if (probe == nullptr) {
        return;
    }
    std::string fresh;
    {
        const ServerOn server;
        fresh = served(probe->m_arguments, probe->m_input).m_output;
    }
    std::string after_history;
    {
        const ServerOn server;
        // Every other kind, in corpus order, as one history.
        for (std::size_t idx = 0; idx < queries.size(); idx += 4) {
            if (&queries[idx] != probe) {
                (void)served(queries[idx].m_arguments, queries[idx].m_input);
            }
        }
        after_history = served(probe->m_arguments, probe->m_input).m_output;
    }
    expect(fresh.find("HOA:") != std::string::npos && fresh == after_history,
           "spot server: one request gets one controller after two "
           "different histories");
}

}  // namespace
