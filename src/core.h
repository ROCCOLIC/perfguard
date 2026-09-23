#pragma once

#include <filesystem>
#include <string>

#include "perfguard_config.h"

namespace perfguard {

struct CommandResult
{
    int exit_code = -1;
    std::string output;

    // True when the child outlived its deadline and was killed, so
    // exit_code is the kill code rather than the child's own result.
    bool timed_out = false;
};


// Runs a shell command and captures its standard output.
//
// capture_stderr merges the child's error stream into the captured
// output and must stay false for any call whose stdout is parsed: on
// failure the child's error text would otherwise be parsed as if it
// were data (e.g. a git error message becoming a commit hash). Set it
// to true only where the output is shown to a human.
//
// The child is killed once timeout_ms elapses, so a stuck build or a
// git command waiting on input cannot hang PerfGuard forever. Pass an
// explicit timeout for builds and benchmarks; they are far slower than
// the git commands the default is sized for.
CommandResult run_command(
    const std::string& command,
    bool capture_stderr,
    int timeout_ms = GIT_TIMEOUT_MS
);


// Returns this process's own temporary directory, creating it on first
// use. Derived from the process id and the clock so that two PerfGuard
// runs can never share a build directory.
std::filesystem::path session_temp_directory();


// Walks up from the current directory to the PerfGuard checkout, which
// is recognised by holding both a .git directory and a CMakeLists.txt.
// Returns an empty path when no ancestor qualifies.
std::filesystem::path find_project_root();


std::string trim(
    std::string value
);


bool is_valid_commit_hash(
    const std::string& commit
);


// Extracts the value of an "Average: N us" line. Returns false when no
// such line exists or the number cannot be parsed.
bool parse_average(
    const std::string& output,
    double& average
);


double calculate_change(
    double baseline,
    double current
);


std::string extract_first_bad_commit(
    const std::string& output
);


// Reports whether an added diff line looks performance-relevant, and
// returns it through evidence when it does.
bool analyze_added_line(
    const std::string& line,
    std::string& evidence
);


// Escapes a string for use inside a JSON string literal.
std::string json_escape(
    const std::string& value
);


// Reads a top-level string or number field out of a JSON object.
//
// Hand-rolled rather than library-backed: the schema is flat, fixed
// and written by this same tool, and a JSON dependency would have to
// be carried by both the tool and the standalone benchmark.
bool json_get_string(
    const std::string& json,
    const std::string& key,
    std::string& value
);

bool json_get_number(
    const std::string& json,
    const std::string& key,
    double& value
);


// ============================================================
// Baseline
//
// The measurement the current tree is compared against, stored in
// .perfguard/baseline.json.
//
// A stored number is only meaningful next to the environment that
// produced it, so each field is recorded and checked before it is
// compared: the same source measured in a different build
// configuration has been seen several percent apart, which is enough
// to decide a verdict whose threshold is ten percent.
//
// schema_version is raised whenever the meaning of a stored field
// changes. Version 1 knew only benchmark/commit/average_us.
// ============================================================

struct Baseline
{
    static constexpr int CURRENT_SCHEMA_VERSION = 2;

    int schema_version = CURRENT_SCHEMA_VERSION;

    std::string benchmark;
    std::string commit;

    double average_us = 0.0;

    // The figures a v2 baseline records. median_us is the one a
    // verdict is drawn from; the rest describe its spread.
    double median_us = 0.0;
    double stdev_us = 0.0;
    double min_us = 0.0;
    double max_us = 0.0;

    int runs = 0;
    int warmup_runs = 0;

    std::string os;
    std::string cpu;
    int cpu_count = 0;
    std::string compiler;
    std::string build_type;
    std::string cmake_generator;
    std::string timestamp;
    std::string perfguard_version;
};


// Fills the fields that describe this machine and this build, leaving
// the measurements and the commit to the caller.
void fill_environment_metadata(
    Baseline& baseline
);


// Reports why a stored baseline may not be compared against a fresh
// measurement from this build: an older schema, a different benchmark,
// or a different build configuration. Returns false with the reason
// filled in. A number measured elsewhere is not evidence about this
// machine, so this is checked before any comparison is reported.
bool baseline_comparable(
    const Baseline& baseline,
    std::string& reason
);


// Reads .perfguard/baseline.json from the project root. Returns false
// when the file is missing or a required field cannot be read, with
// the reason in error.
bool read_baseline(
    const std::filesystem::path& project_root,
    Baseline& baseline,
    std::string& error
);


// Writes .perfguard/baseline.json, creating .perfguard/ if needed.
bool write_baseline(
    const std::filesystem::path& project_root,
    const Baseline& baseline,
    std::string& error
);

}  // namespace perfguard
