#pragma once

/// @file accumulator.hpp
/// @brief Cross-generation collection of the repairs a run passes over, and
///        the incremental writer that puts each one on disk as it is found.

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/// Process-wide record of what the accumulator added to a run's output.
struct AccumulatorStats {
    /// Specifications the accumulator contributed that the final population's
    /// own collection did not already hold. Zero when every accumulated repair
    /// happened to survive into the last generation.
    inline static std::size_t n_contributed{0};
};

/// Writes each newly accumulated specification into `<output-dir>/accumulated/`
/// the moment it is accumulated, one file per specification.
///
/// Holding the accumulated set in memory until the search finishes loses all of
/// it to an external wall-clock cap, which is not a hypothetical failure: the
/// AuRUS baseline has exactly that design, and in the aurus-h2h campaign 126 of
/// its runs were killed at the 7200s cap and lost 3,934 solutions their own
/// logs prove had been found. Each file here is opened, written and closed on
/// the spot, so a `SIGKILL` at any moment leaves every file already written
/// intact.
///
/// What lands there is the *raw* set of candidates that passed the output gate,
/// and is deliberately not the run's answer: `repair_N.json` / `repair_N.tlsf`
/// remain the only filtered output, carrying the deduplicated, weakening- and
/// maximality-screened set with its fitness record. A reader that treats the
/// accumulated files as repairs is reading candidates the final filters may
/// well have rejected.
///
/// Nothing touches the filesystem before the first write, so neither the
/// directory nor a file exists until a candidate is accumulated.
class AccumulatedRepairWriter {
   public:
    /// A default-constructed writer writes nothing, for the callers that
    /// accumulate in memory alone, such as the tests.
    AccumulatedRepairWriter() = default;

    /// @p extension names the path's own format, `.json` or `.tlsf`.
    /// @p elapsed reports seconds since the search began, and is what stamps
    /// each candidate into the index. A writer given none still writes the
    /// specification files; its index reports an elapsed time of zero, which is
    /// what the tests want and what a caller with no run clock can offer.
    AccumulatedRepairWriter(const std::string& output_dir,
                            std::string extension,
                            std::function<double()> elapsed = {});

    /// False for a default-constructed writer and after the first failure, so
    /// a caller can skip rendering a document nothing will write.
    [[nodiscard]] bool active() const;

    /// Writes @p document, one specification already rendered by the path's
    /// own serialiser. @p generation is 1-indexed and names the file, so the
    /// origin of an accumulated repair is legible without opening it; a
    /// run-wide sequence number follows it, which makes a collision impossible
    /// whatever order the generations contribute in. Returns the file's name
    /// within the accumulated directory, or an empty string where nothing was
    /// written.
    std::string write(std::size_t generation, const std::string& document);

    /// The directory under `<output-dir>` the files go into.
    static constexpr const char* k_subdirectory = "accumulated";

   private:
    static constexpr const char* k_index_name = "index.tsv";

    void append_index_row(const std::string& file_name, std::size_t generation);
    bool create_directory();
    void warn(const std::string& message);

    std::string m_directory;
    std::string m_extension;
    std::function<double()> m_elapsed;
    std::size_t m_sequence{0};
    bool m_created{false};
    bool m_failed{false};
};

/// Collects the specifications that passed the output gate in any generation,
/// deduplicated and uncapped, in the order they were first seen.
///
/// A run reports the maximal antichain of its *final* population, so a
/// candidate that passed the gate in generation 3 and was not selected into
/// generation 4 is a repair the search found and then discarded. Repair quality
/// is judged existentially over the emitted set, so keeping such a candidate
/// can only add to what the run reports.
///
/// It only ever reads, and never draws from the RandomSource, so it leaves the
/// seed stream as it found it.
template <typename Spec>
class RepairAccumulator {
   public:
    /// Called once per newly accumulated specification, after its file is
    /// written, with the file's name, or an empty string where the writer wrote
    /// none. The streaming implication filter is fed through this.
    using Sink = std::function<void(const Spec&, const std::string&)>;

    /// Renders one specification as the document @p writer puts on disk. Pass
    /// the path's own serialiser -- `to_json` or `tlsf::write` -- so an
    /// accumulated file reads exactly like a repair of the same specification,
    /// tombstoned guarantees omitted (see "Removable guarantees" in
    /// docs/dev/operators.md).
    using Render = std::function<std::string(const Spec&)>;

    /// With no @p render, nothing is written whatever @p writer is.
    explicit RepairAccumulator(AccumulatedRepairWriter writer = {},
                               Render render = {}, Sink sink = {})
        : m_writer(std::move(writer)),
          m_render(std::move(render)),
          m_sink(std::move(sink)) {}

    /// @p generation is 1-indexed, and reaches the writer as part of the file
    /// name.
    ///
    /// Both accumulation sites run on the driver thread -- the FRETISH sweep
    /// inside the generation loop, the TLSF one over the verdicts
    /// `gate_verdicts` has already returned -- so neither is inside the scoring
    /// pool and neither the set nor the writer needs a lock.
    void insert(const Spec& spec, std::size_t generation) {
        if (!m_seen.insert(spec).second) {
            return;
        }
        m_specifications.push_back(spec);
        const std::string name =
            m_render && m_writer.active()
                ? m_writer.write(generation, m_render(spec))
                : std::string();
        if (m_sink) {
            m_sink(spec, name);
        }
    }

    /// First-seen order rather than the hash set's, so what comes back is a
    /// function of the search rather than of the container's bucket layout.
    [[nodiscard]] const std::vector<Spec>& specifications() const {
        return m_specifications;
    }

   private:
    AccumulatedRepairWriter m_writer;
    Render m_render;
    Sink m_sink;
    std::unordered_set<Spec> m_seen;
    std::vector<Spec> m_specifications;
};

/// Appends the accumulated specifications that @p into does not already hold,
/// returning how many were added.
///
/// The accumulated members passed the same gate the collection in @p into
/// applies, in the generation they were collected in, so they are merged rather
/// than re-checked.
template <typename Spec>
std::size_t merge_accumulated(std::vector<Spec>& into,
                              const std::vector<Spec>& accumulated) {
    std::unordered_set<Spec> present(into.begin(), into.end());
    std::size_t added = 0;
    for (const Spec& spec : accumulated) {
        if (!present.insert(spec).second) {
            continue;
        }
        into.push_back(spec);
        ++added;
    }
    return added;
}
