#include "bisect.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "core.h"
#include "perfguard_config.h"

using namespace std;
using namespace perfguard;

namespace fs = std::filesystem;

namespace {

// ============================================================
// Build directory guard
//
// This executable runs once per Git bisect step, so every one of
// its exit paths must take the build directory with it. Nothing
// else is left to clean up afterwards.
// ============================================================

struct BuildDirectoryGuard
{
    fs::path path;

    ~BuildDirectoryGuard()
    {
        error_code error;

        fs::remove_all(
            path,
            error
        );
    }
};


// ============================================================
// Bisect session guard
//
// `git bisect` moves HEAD and keeps its state inside .git, so a run
// that ends early leaves the repository in the middle of a bisect:
// parked on some commit, with the branch it started from recorded
// only in Git's own files. The guard refuses to start on top of a
// session it did not begin, and puts HEAD back however this process
// exits -- including the paths that do not run destructors.
// ============================================================

bool bisect_in_progress(
    const fs::path& root
)
{
    const CommandResult result =
        run_command(
            "git -C \"" +
            root.string() +
            "\" rev-parse --git-path BISECT_START",
            false
        );

    if (
        result.exit_code != 0
    ) {
        return false;
    }

    const string path =
        trim(result.output);

    if (
        path.empty()
    ) {
        return false;
    }

    error_code error;

    return fs::exists(
        fs::path(path),
        error
    );
}


string read_revision(
    const fs::path& root,
    const string& revision
)
{
    const CommandResult result =
        run_command(
            "git -C \"" +
            root.string() +
            "\" rev-parse " +
            revision,
            false
        );

    if (
        result.exit_code != 0
    ) {
        return string();
    }

    return trim(result.output);
}


// Returns HEAD to where it was. `git bisect reset` checks out the
// branch that was current when the session began; when that did not
// happen, the recorded reference is checked out explicitly, so that
// no failure here can leave the caller standing on a bisect commit.
void restore_head(
    const fs::path& root,
    const string& head,
    const string& branch
)
{
    run_command(
        "git -C \"" +
        root.string() +
        "\" bisect reset",
        true
    );

    if (
        head.empty() ||
        read_revision(root, "HEAD") == head
    ) {
        return;
    }

    // rev-parse --abbrev-ref prints "HEAD" while detached.
    const string reference =
        branch.empty() || branch == "HEAD"
            ? head
            : branch;

    run_command(
        "git -C \"" +
        root.string() +
        "\" checkout \"" +
        reference +
        "\"",
        true
    );
}


struct BisectGuard;

// The session being watched over, so that the exit paths which skip
// destructors can still reset. Cleared by the guard itself, so the
// handlers below never touch a guard that has gone out of scope.
BisectGuard* g_active_bisect_guard = nullptr;


struct BisectGuard
{
    fs::path root;
    string head;
    string branch;

    bool active = true;

    ~BisectGuard()
    {
        release();
    }

    void release()
    {
        if (
            !active
        ) {
            return;
        }

        active = false;

        if (
            g_active_bisect_guard == this
        ) {
            g_active_bisect_guard = nullptr;
        }

        restore_head(root, head, branch);
    }
};


void reset_interrupted_bisect()
{
    if (
        g_active_bisect_guard != nullptr
    ) {
        g_active_bisect_guard->release();
    }
}


BOOL WINAPI bisect_console_handler(
    DWORD control_type
)
{
    reset_interrupted_bisect();

    // Returning false lets the default handler end the process; the
    // repository has already been put back.
    return FALSE;
}

} // namespace

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
    double threshold = DEFAULT_THRESHOLD_PERCENT;

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
    // External build directory
    //
    // Unique to this process, so a concurrently running PerfGuard
    // cannot be building into the same tree.
    // --------------------------------------------------------

    const fs::path build_dir =
        session_temp_directory() /
        "bisect-build";

    const BuildDirectoryGuard build_guard{build_dir};

    // --------------------------------------------------------
    // Configure
    // --------------------------------------------------------

    cout
        << "[1/3] Configuring...\n";

    string configure_command =
        "cmake -S \"" +
        root.string() +
        "\" -B \"" +
        build_dir.string() +
        "\" -G \"Visual Studio 17 2022\"";

    CommandResult configure =
        run_command(
            configure_command,
            true,
            BUILD_TIMEOUT_MS
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
        build_dir.string() +
        "\" --config Debug";

    CommandResult build =
        run_command(
            build_command,
            true,
            BUILD_TIMEOUT_MS
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
        build_dir /
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

    // The average is parsed out of this output, so stderr must not be
    // merged into it.
    CommandResult benchmark =
        run_command(
            benchmark_command,
            false,
            BENCHMARK_TIMEOUT_MS
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
// Automatic Bisect
// ============================================================

BisectResult run_bisect(
    const string& executable_path,
    bool force
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
        !is_valid_commit_hash(
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
            "\" status --porcelain",
            true
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
    // Refuse to run on top of a session we did not start
    //
    // A bisect in progress belongs to whoever left it there, and
    // resetting it here would discard their bisect state and move
    // their HEAD. Only --force clears it.
    // --------------------------------------------------------

    if (
        bisect_in_progress(root)
    )
    {
        if (
            !force
        )
        {
            result.error =
                "A git bisect session is already in progress.\n"
                "Run: git bisect reset\n"
                "or pass --force to discard it.";

            return result;
        }

        run_command(
            "git -C \"" +
            root.string() +
            "\" bisect reset",
            true
        );
    }

    // --------------------------------------------------------
    // Take responsibility for HEAD from here on
    // --------------------------------------------------------

    BisectGuard guard;

    guard.root = root;
    guard.head = read_revision(root, "HEAD");
    guard.branch = read_revision(root, "--abbrev-ref HEAD");

    g_active_bisect_guard = &guard;

    // The guard's destructor covers an ordinary return; these cover
    // the exits that do not run destructors, such as Ctrl-C and a
    // console window being closed.
    atexit(reset_interrupted_bisect);
    SetConsoleCtrlHandler(bisect_console_handler, TRUE);

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
            "\" bisect start",
            true
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
            "\" bisect bad HEAD",
            true
        );

    if (bad.exit_code != 0)
    {
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
            result.baseline_commit,
            true
        );

    if (good.exit_code != 0)
    {
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
        to_string(DEFAULT_THRESHOLD_PERCENT);

    CommandResult bisect =
        run_command(
            command,
            true
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
    // Reset bisect state now, so that a failure to get back to the
    // starting commit is reported rather than only done silently by
    // the guard.
    // --------------------------------------------------------

    guard.release();

    if (
        !guard.head.empty() &&
        read_revision(root, "HEAD") != guard.head
    )
    {
        result.error =
            "Bisect finished but the repository was not "
            "restored to its original HEAD.";

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