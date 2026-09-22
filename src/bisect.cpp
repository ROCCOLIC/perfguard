#include "bisect.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

using namespace std;

namespace fs = std::filesystem;

// ============================================================
// Configuration
// ============================================================

const double DEFAULT_THRESHOLD = 10.0;

const fs::path BISECT_BUILD_DIR =
    "C:/perfguard-bisect-build";

// ============================================================
// Utility
// ============================================================

struct CommandResult
{
    int exit_code = -1;
    string output;
};

CommandResult run_command(
    const string& command
)
{
    CommandResult result;

    string full_command =
        command + " 2>&1";

    FILE* pipe =
        _popen(
            full_command.c_str(),
            "r"
        );

    if (pipe == nullptr)
    {
        result.output =
            "Failed to start command.";

        return result;
    }

    array<char, 4096> buffer{};

    while (
        fgets(
            buffer.data(),
            static_cast<int>(buffer.size()),
            pipe
        ) != nullptr
    )
    {
        result.output +=
            buffer.data();
    }

    result.exit_code =
        _pclose(pipe);

    return result;
}

// ============================================================
// String helpers
// ============================================================

string trim(
    string value
)
{
    while (
        !value.empty() &&
        (
            value.back() == '\n' ||
            value.back() == '\r' ||
            value.back() == ' ' ||
            value.back() == '\t'
        )
    )
    {
        value.pop_back();
    }

    size_t start = 0;

    while (
        start < value.size() &&
        (
            value[start] == ' ' ||
            value[start] == '\t'
        )
    )
    {
        ++start;
    }

    return value.substr(start);
}

bool is_hex_commit(
    const string& commit
)
{
    if (
        commit.size() < 7 ||
        commit.size() > 64
    )
    {
        return false;
    }

    for (char c : commit)
    {
        if (!(
            (c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F')
        ))
        {
            return false;
        }
    }

    return true;
}

// ============================================================
// Project root
// ============================================================

bool is_project_root(
    const fs::path& path
)
{
    return
        fs::exists(path / ".git") &&
        fs::exists(path / "CMakeLists.txt");
}

fs::path find_project_root()
{
    fs::path current =
        fs::current_path();

    while (true)
    {
        if (is_project_root(current))
        {
            return current;
        }

        if (current == current.root_path())
        {
            break;
        }

        current =
            current.parent_path();
    }

    return {};
}

// ============================================================
// Baseline
// ============================================================

bool read_baseline(
    const fs::path& root,
    string& commit,
    double& average_us
)
{
    fs::path baseline_path =
        root /
        ".perfguard" /
        "baseline.json";

    if (!fs::exists(baseline_path))
    {
        return false;
    }

    ifstream file(
        baseline_path
    );

    if (!file)
    {
        return false;
    }

    string content(
        (
            istreambuf_iterator<char>(file)
        ),
        istreambuf_iterator<char>()
    );

    regex commit_pattern(
        R"PG("commit"\s*:\s*"([^"]+)")PG"
    );

    regex average_pattern(
        R"PG("average_us"\s*:\s*([0-9]+(?:\.[0-9]+)?))PG"
    );

    smatch match;

    if (
        !regex_search(
            content,
            match,
            commit_pattern
        )
    )
    {
        return false;
    }

    commit =
        match[1].str();

    if (
        !regex_search(
            content,
            match,
            average_pattern
        )
    )
    {
        return false;
    }

    average_us =
        stod(
            match[1].str()
        );

    return true;
}

// ============================================================
// Benchmark result parsing
// ============================================================

bool parse_average(
    const string& output,
    double& average
)
{
    regex pattern(
        R"(Average:\s+([0-9]+(?:\.[0-9]+)?)\s+us)"
    );

    smatch match;

    if (
        regex_search(
            output,
            match,
            pattern
        )
    )
    {
        average =
            stod(
                match[1].str()
            );

        return true;
    }

    return false;
}

// ============================================================
// Bisect test
//
// This function is executed by the CURRENT PerfGuard binary.
//
// The source tree may have been checked out to an old commit,
// but this executable itself remains the new PerfGuard binary.
//
// The actual benchmark is the standalone:
//
//     sort_1m.exe
//
// Exit code:
//   0   = good
//   1   = bad / regression
//   125 = unable to test
// ============================================================

int run_bisect_test(
    const string& baseline_us_text,
    const string& threshold_text
)
{
    double baseline_us = 0.0;
    double threshold = DEFAULT_THRESHOLD;

    try
    {
        baseline_us =
            stod(
                baseline_us_text
            );

        threshold =
            stod(
                threshold_text
            );
    }
    catch (...)
    {
        cerr
            << "Invalid bisect parameters.\n";

        return 125;
    }

    if (baseline_us <= 0.0)
    {
        cerr
            << "Invalid baseline.\n";

        return 125;
    }

    fs::path root =
        find_project_root();

    if (root.empty())
    {
        cerr
            << "Could not find project root.\n";

        return 125;
    }

    cout
        << "\n----------------------------------------\n";

    cout
        << "PerfGuard Bisect Test\n";

    cout
        << "----------------------------------------\n";

    // --------------------------------------------------------
    // Clean external build directory
    // --------------------------------------------------------

    std::error_code ec;

    fs::remove_all(
        BISECT_BUILD_DIR,
        ec
    );

    // --------------------------------------------------------
    // Configure
    // --------------------------------------------------------

    cout
        << "[1/3] Configuring...\n";

    string configure_command =
        "cmake -S \"" +
        root.string() +
        "\" -B \"" +
        BISECT_BUILD_DIR.string() +
        "\" -G \"Visual Studio 17 2022\"";

    CommandResult configure =
        run_command(
            configure_command
        );

    if (configure.exit_code != 0)
    {
        cout
            << configure.output;

        cerr
            << "CMake configuration failed.\n";

        return 125;
    }

    // --------------------------------------------------------
    // Build
    // --------------------------------------------------------

    cout
        << "[2/3] Building...\n";

    string build_command =
        "cmake --build \"" +
        BISECT_BUILD_DIR.string() +
        "\" --config Debug";

    CommandResult build =
        run_command(
            build_command
        );

    if (build.exit_code != 0)
    {
        cout
            << build.output;

        cerr
            << "Build failed.\n";

        return 125;
    }

    // --------------------------------------------------------
    // Run standalone benchmark
    // --------------------------------------------------------

    cout
        << "[3/3] Running benchmark...\n";

    fs::path executable =
        BISECT_BUILD_DIR /
        "Debug" /
        "sort_1m.exe";

    if (!fs::exists(executable))
    {
        cerr
            << "Standalone benchmark executable not found:\n"
            << executable.string()
            << "\n";

        return 125;
    }

    string benchmark_command =
        "\"" +
        executable.string() +
        "\"";

    CommandResult benchmark =
        run_command(
            benchmark_command
        );

    // --------------------------------------------------------
    // Parse benchmark result
    //
    // sort_1m.exe is responsible only for measuring time.
    //
    // It should normally return 0.
    //
    // We therefore use its output, rather than its exit code,
    // as the source of the measured performance value.
    // --------------------------------------------------------

    double current_us = 0.0;

    if (
        !parse_average(
            benchmark.output,
            current_us
        )
    )
    {
        cout
            << benchmark.output;

        cerr
            << "Could not parse benchmark average.\n";

        return 125;
    }

    double change =
        (
            (
                current_us -
                baseline_us
            )
            /
            baseline_us
        )
        *
        100.0;

    cout
        << "\nBenchmark: sort_1m\n";

    cout
        << "Baseline: "
        << baseline_us
        << " us\n";

    cout
        << "Current:  "
        << current_us
        << " us\n";

    cout
        << "Change:   "
        << change
        << "%\n";

    cout
        << "Threshold: "
        << threshold
        << "%\n";

    if (change > threshold)
    {
        cout
            << "Result: REGRESSION\n";

        return 1;
    }

    cout
        << "Result: OK\n";

    return 0;
}

// ============================================================
// Extract first bad commit
// ============================================================

string extract_first_bad_commit(
    const string& output
)
{
    regex pattern(
        R"(([0-9a-fA-F]{7,64})\s+is the first bad commit)"
    );

    smatch match;

    if (
        regex_search(
            output,
            match,
            pattern
        )
    )
    {
        return match[1].str();
    }

    return {};
}

// ============================================================
// Automatic Bisect
// ============================================================

BisectResult run_bisect(
    const string& executable_path
)
{
    BisectResult result;

    fs::path root =
        find_project_root();

    if (root.empty())
    {
        result.error =
            "Could not find project root.";

        return result;
    }

    // --------------------------------------------------------
    // Read baseline BEFORE Git starts changing HEAD
    // --------------------------------------------------------

    double baseline_us = 0.0;

    if (
        !read_baseline(
            root,
            result.baseline_commit,
            baseline_us
        )
    )
    {
        result.error =
            "Could not read baseline.";

        return result;
    }

    if (
        !is_hex_commit(
            result.baseline_commit
        )
    )
    {
        result.error =
            "Invalid baseline commit.";

        return result;
    }

    // --------------------------------------------------------
    // Check working tree
    // --------------------------------------------------------

    CommandResult status =
        run_command(
            "git -C \"" +
            root.string() +
            "\" status --porcelain"
        );

    if (
        status.exit_code != 0
    )
    {
        result.error =
            "Could not read Git status.";

        return result;
    }

    if (
        !trim(status.output).empty()
    )
    {
        result.error =
            "Working tree is not clean.";

        return result;
    }

    // --------------------------------------------------------
    // Reset any previous bisect session
    // --------------------------------------------------------

    run_command(
        "git -C \"" +
        root.string() +
        "\" bisect reset"
    );

    // --------------------------------------------------------
    // Start bisect
    // --------------------------------------------------------

    cout
        << "========================================\n";

    cout
        << "PerfGuard Automatic Bisect\n";

    cout
        << "========================================\n";

    cout
        << "\nBaseline commit: "
        << result.baseline_commit
        << "\n";

    cout
        << "Baseline average: "
        << baseline_us
        << " us\n";

    cout
        << "\nStarting Git bisect...\n\n";

    CommandResult start =
        run_command(
            "git -C \"" +
            root.string() +
            "\" bisect start"
        );

    if (start.exit_code != 0)
    {
        result.error =
            "Failed to start Git bisect.\n" +
            start.output;

        return result;
    }

    // --------------------------------------------------------
    // Mark current HEAD as bad
    // --------------------------------------------------------

    CommandResult bad =
        run_command(
            "git -C \"" +
            root.string() +
            "\" bisect bad HEAD"
        );

    if (bad.exit_code != 0)
    {
        run_command(
            "git -C \"" +
            root.string() +
            "\" bisect reset"
        );

        result.error =
            "Failed to mark HEAD as bad.\n" +
            bad.output;

        return result;
    }

    // --------------------------------------------------------
    // Mark baseline as good
    // --------------------------------------------------------

    CommandResult good =
        run_command(
            "git -C \"" +
            root.string() +
            "\" bisect good " +
            result.baseline_commit
        );

    if (good.exit_code != 0)
    {
        run_command(
            "git -C \"" +
            root.string() +
            "\" bisect reset"
        );

        result.error =
            "Failed to mark baseline as good.\n" +
            good.output;

        return result;
    }

    // --------------------------------------------------------
    // Run current PerfGuard executable as test runner
    //
    // IMPORTANT:
    //
    // The PerfGuard executable itself is NOT the benchmark.
    //
    // It only acts as the Git bisect test runner and invokes
    // sort_1m.exe inside run_bisect_test().
    // --------------------------------------------------------

    string runner =
        "\"" +
        executable_path +
        "\"";

    string command =
        "git -C \"" +
        root.string() +
        "\" bisect run " +
        runner +
        " --bisect-test " +
        to_string(baseline_us) +
        " " +
        to_string(DEFAULT_THRESHOLD);

    CommandResult bisect =
        run_command(
            command
        );

    result.output =
        bisect.output;

    // --------------------------------------------------------
    // Find first bad commit
    // --------------------------------------------------------

    result.first_bad_commit =
        extract_first_bad_commit(
            bisect.output
        );

    // --------------------------------------------------------
    // Always reset bisect state
    // --------------------------------------------------------

    CommandResult reset =
        run_command(
            "git -C \"" +
            root.string() +
            "\" bisect reset"
        );

    if (
        reset.exit_code != 0
    )
    {
        result.error =
            "Bisect finished but failed to reset Git state.\n" +
            reset.output;

        return result;
    }

    if (
        result.first_bad_commit.empty()
    )
    {
        result.error =
            "Git bisect did not identify a first bad commit.";

        return result;
    }

    result.success = true;

    return result;
}