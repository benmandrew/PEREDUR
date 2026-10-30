#include "genetic/accumulator.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

AccumulatedRepairWriter::AccumulatedRepairWriter(
    const std::string& output_dir, std::string extension,
    std::function<double()> elapsed)
    : m_directory(
          (std::filesystem::path(output_dir) / k_subdirectory).string()),
      m_extension(std::move(extension)),
      m_elapsed(std::move(elapsed)) {}

bool AccumulatedRepairWriter::active() const {
    return !m_directory.empty() && !m_failed;
}

std::string AccumulatedRepairWriter::write(std::size_t generation,
                                           const std::string& document) {
    if (!active()) {
        return {};
    }
    if (!m_created && !create_directory()) {
        return {};
    }
    std::ostringstream name;
    name << "gen" << std::setw(2) << std::setfill('0') << generation << "_"
         << std::setw(4) << std::setfill('0') << m_sequence << m_extension;
    const std::string path =
        (std::filesystem::path(m_directory) / name.str()).string();
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) {
        warn("could not open " + path);
        return {};
    }
    file << document;
    // Closed here rather than left to the destructor: the buffer has to reach
    // the kernel before the next candidate is scored, or a kill in between
    // loses a repair that this call reported as written.
    file.close();
    if (!file) {
        warn("could not write " + path);
        return {};
    }
    append_index_row(name.str(), generation);
    ++m_sequence;
    return name.str();
}

// One flushed row per accumulated candidate: which file, which generation, and
// how many seconds into the search it passed the gate.
//
// The generation is already in the file name and is repeated here so a reader
// needs no filename parsing, and the elapsed time is here rather than in the
// name because the name is an artefact format that campaign scripts already
// glob. Opened in append mode and closed per row, for the reason the
// specification files are: a run killed by a wall-clock cap has to keep the
// rows it had already written.
void AccumulatedRepairWriter::append_index_row(const std::string& file_name,
                                               std::size_t generation) {
    const std::string path =
        (std::filesystem::path(m_directory) / k_index_name).string();
    const bool first = m_sequence == 0;
    std::ofstream index(path, std::ios::out | std::ios::app);
    if (!index) {
        warn("could not open " + path);
        return;
    }
    if (first) {
        index << "file\tgeneration\telapsed_s\n";
    }
    index << file_name << "\t" << generation << "\t" << std::fixed
          << std::setprecision(6) << (m_elapsed ? m_elapsed() : 0.0) << "\n";
    index.close();
    if (!index) {
        warn("could not write " + path);
    }
}

bool AccumulatedRepairWriter::create_directory() {
    std::error_code error;
    std::filesystem::create_directories(m_directory, error);
    if (error) {
        warn("could not create " + m_directory + ": " + error.message());
        return false;
    }
    m_created = true;
    return true;
}

// Reports once and then stops trying. Losing the accumulated files must never
// take the repair run with them.
void AccumulatedRepairWriter::warn(const std::string& message) {
    m_failed = true;
    std::cerr << "warning: " << message
              << "; the run continues without accumulated repair files\n";
}
