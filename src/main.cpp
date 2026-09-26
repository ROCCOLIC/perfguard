#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core.h"
#include "perfguard_config.h"
#include "evidence.h"
#include "diagnosis.h"
#include "bisect.h"
#include "measurement.h"

using namespace std;
using namespace perfguard;

namespace fs = std::filesystem;


// ============================================================
// Configuration
// ============================================================

const string BASELINE_PATH =
    ".perfguard/baseline.json";


// ============================================================
// Git commit
// ============================================================

string get_git_commit()
{
    string result =
        run_command(
            "git rev-parse HEAD",
            false
        ).output;

    if (
        result.empty()
    ) {
        return {};
    }

    while (
        !result.empty() &&
        (
            result.back() == '\n' ||
            result.back() == '\r'
        )
    ) {
        result.pop_back();
    }

    return result;
}


// ============================================================
// Benchmark
// ============================================================

fs::path get_benchmark_executable(
    const fs::path& perfguard_executable
)
{
    return
        perfguard_executable
            .parent_path()
        /
        "sort_1m.exe";
}


// ============================================================
// Artificial regression benchmark
//
// Carries its own reference workload rather than borrowing the
// sort_1m baseline: two different benchmarks have no comparable
// scale, and comparing them measured the second one as 32% faster
// than the first, which no slowdown could ever turn into a
// regression.
// ============================================================

double run_artificial_regression_benchmark(
    int slowdown_ms
)
{
    vector<int> data(
        BENCHMARK_SIZE
    );

    double total_us = 0.0;

    for (
        int run = 0;
        run < BENCHMARK_RUNS;
        ++run
    )
    {
        for (
            int i = 0;
            i < BENCHMARK_SIZE;
            ++i
        ) {
            data[i] =
                (
                    BENCHMARK_SIZE -
                    i
                ) % 100000;
        }

        auto start =
            chrono::high_resolution_clock::now();

        sort(
            data.begin(),
            data.end()
        );

        // ----------------------------------------------------
        // Artificial slowdown
        // ----------------------------------------------------

        this_thread::sleep_for(
            chrono::milliseconds(slowdown_ms)
        );

        auto end =
            chrono::high_resolution_clock::now();

        chrono::duration<double, micro> elapsed =
            end - start;

        total_us +=
            elapsed.count();
    }

    return
        total_us /
        BENCHMARK_RUNS;
}


// ============================================================
// Benchmark output
// ============================================================

void print_benchmark_result(
    double average
)
{
    cout
        << "Average: "
        << fixed
        << setprecision(2)
        << average
        << " us\n";
}


// ============================================================
// Baseline
//
// A stored measurement describes one machine in one build
// configuration. Two builds of identical source have been measured
// several percent apart, which decides a verdict whose threshold is
// ten percent, so a stored number is only compared when the
// environment that produced it matches this one.
// ============================================================

// Loads the stored baseline and checks it against this build.
//
// Returns false with a message and an exit code: 2 throughout, because
// a comparison that cannot be made must not be reported as a verdict.
bool load_comparable_baseline(
    Baseline& baseline,
    string& message,
    int& exit_code
)
{
    exit_code = 2;

    const fs::path project_root =
        find_project_root();

    if (
        project_root.empty()
    ) {
        message =
            "Unable to find the PerfGuard project root. "
            "Make sure the command is executed inside "
            "the PerfGuard project directory.";

        return false;
    }

    string error;

    if (
        !read_baseline(
            project_root,
            baseline,
            error
        )
    ) {
        message =
            error +
            "\n  Run:\n"
            "    perfguard baseline";

        return false;
    }

    string reason;

    if (
        !baseline_comparable(
            baseline,
            reason
        )
    ) {
        message =
            reason +
            "\n  Run:\n"
            "    perfguard baseline";

        return false;
    }

    return true;
}


// ============================================================
// Show commit evidence
// ============================================================

// Returns the process exit code: a commit whose evidence cannot be
// read is a failed command, not a report about the commit.
int show_commit_evidence(
    const string& commit
)
{
    EvidenceReport report =
        analyze_commit(
            commit
        );

    if (
        !report.success
    ) {
        cerr
            << "Evidence analysis failed: "
            << report.error
            << "\n";

        return 2;
    }

    cout
        << "\n";

    cout
        << "========================================\n";

    cout
        << "Commit Evidence\n";

    cout
        << "========================================\n";

    cout
        << "\n";

    cout
        << "Commit: "
        << report.commit
        << "\n";

    cout
        << "Message: "
        << report.message
        << "\n";

    cout
        << "Author: "
        << report.author
        << "\n";

    cout
        << "\nChanged files:\n";

    for (
        const auto& file :
        report.changed_files
    ) {
        cout
            << "  "
            << file
            << "\n";
    }

    cout
        << "\nChange summary:\n";

    cout
        << "  Lines added:   "
        << report.lines_added
        << "\n";

    cout
        << "  Lines removed: "
        << report.lines_removed
        << "\n";

    cout
        << "\nPotential performance evidence:\n";

    if (
        report.performance_evidence.empty()
    ) {
        cout
            << "  None detected.\n";
    }
    else {
        for (
            const auto& line :
            report.performance_evidence
        ) {
            cout
                << "  "
                << line
                << "\n";
        }
    }

    cout
        << "\n";

    return 0;
}


// ============================================================
// Measurement summary
// ============================================================

void print_stats(
    const string& label,
    const Stats& stats
)
{
    cout
        << "\n"
        << label
        << ": "
        << stats.samples
        << " sample(s)";

    if (
        stats.outliers_removed > 0
    ) {
        cout
            << ", "
            << stats.outliers_removed
            << " outlier(s) removed";
    }
    else if (
        !stats.outlier_check
    ) {
        cout
            << ", too few samples "
               "for outlier rejection";
    }

    cout
        << "\n";

    cout
        << "  min "
        << fixed
        << setprecision(2)
        << stats.min_us
        << "  median "
        << fixed
        << setprecision(2)
        << stats.median_us
        << "  max "
        << fixed
        << setprecision(2)
        << stats.max_us
        << "  stdev "
        << fixed
        << setprecision(2)
        << stats.stdev_us
        << " us\n";
}


// ============================================================
// Command line
// ============================================================

struct Options
{
    bool json = false;
    bool help = false;
    bool version = false;

    // --bisect clears a bisect session it finds already running, which
    // otherwise stops it: the session belongs to whoever left it.
    bool force = false;

    // Set when an option could not be understood. The message is
    // reported and usage printed instead of running anything.
    string error;

    double threshold_percent =
        DEFAULT_THRESHOLD_PERCENT;

    // Samples to take, 0 meaning the mode's own default.
    int runs = 0;

    vector<string> arguments;
};


string usage_text()
{
    return
        "PerfGuard - performance regression detection\n"
        "\n"
        "Usage:\n"
        "  perfguard <command> [arguments] [options]\n"
        "\n"
        "Commands:\n"
        "  baseline                 Measure the current tree and store it\n"
        "                           as the baseline to compare against\n"
        "  --check                  Build and measure both the baseline\n"
        "                           commit and HEAD, then compare medians\n"
        "  --report                 Compare the stored baseline against a\n"
        "                           fresh measurement, and write a report\n"
        "  --analyze                Compare the stored baseline against\n"
        "                           the working tree, then bisect,\n"
        "                           diagnose and report\n"
        "  --bisect                 Find the first bad commit\n"
        "  --diagnose <commit>      Build one commit and compare it\n"
        "  --evidence <commit>      Show what a commit changed\n"
        "  --simulate-regression    Self-test: inject a slowdown and look\n"
        "                           for it\n"
        "  --bisect-test <us> <pct> Internal: one git bisect step\n"
        "  --help, -h               Show this text\n"
        "  --version, -V            Show the version\n"
        "\n"
        "Options:\n"
        "  --threshold <percent>    Regression threshold (default 10)\n"
        "  --runs <n>               Samples to take (default: per command)\n"
        "  --force                  --bisect and --analyze: discard a\n"
        "                           bisect session that is already in\n"
        "                           progress\n"
        "  --json                   Machine-readable output on stdout\n"
        "                           (supported by baseline, the plain\n"
        "                           benchmark run, --check and --diagnose)\n"
        "\n"
        "Exit codes:\n"
        "  0  no regression\n"
        "  1  regression detected\n"
        "  2  error, or the stored baseline cannot be compared\n"
        "\n"
        "  --bisect-test is an internal step of git bisect and keeps its\n"
        "  own contract: 0 = good, 1 = bad, 125 = skip.\n";
}


// The commands this tool accepts, so that a leading dash can still
// mean "unknown option" for anything else.
bool is_known_command(
    const string& argument
)
{
    static const char* COMMANDS[] = {
        "baseline",
        "--check",
        "--report",
        "--analyze",
        "--bisect",
        "--diagnose",
        "--evidence",
        "--simulate-regression",
        "--bisect-test"
    };

    for (
        const char* command :
        COMMANDS
    ) {
        if (
            argument == command
        ) {
            return true;
        }
    }

    return false;
}


// Arguments a command cannot run without, counting the command
// itself. A command that takes none is one, so that it is never
// reported as missing something.
int required_arguments(
    const string& command
)
{
    if (
        command == "--bisect-test"
    ) {
        return 3;
    }

    if (
        command == "--diagnose" ||
        command == "--evidence"
    ) {
        return 2;
    }

    return 1;
}


// Parses the whole command line in one pass, so that options may be
// written before or after the command: "perfguard --check --threshold 5"
// and "perfguard --threshold 5 --check" mean the same thing.
Options parse_options(
    int argc,
    char* argv[]
)
{
    Options options;

    for (
        int index = 1;
        index < argc;
        ++index
    ) {
        const string argument =
            argv[index];

        if (
            argument == "--json"
        ) {
            options.json = true;

            continue;
        }

        if (
            argument == "--force"
        ) {
            options.force = true;

            continue;
        }

        if (
            argument == "--help" ||
            argument == "-h"
        ) {
            options.help = true;

            continue;
        }

        if (
            argument == "--version" ||
            argument == "-V" ||
            argument == "-v"
        ) {
            options.version = true;

            continue;
        }

        if (
            argument == "--threshold" ||
            argument == "--runs"
        ) {
            if (
                index + 1 >= argc
            ) {
                options.error =
                    argument +
                    " needs a value.";

                return options;
            }

            const string value =
                argv[++index];

            try {
                if (
                    argument == "--threshold"
                ) {
                    options.threshold_percent =
                        stod(value);
                }
                else {
                    options.runs =
                        stoi(value);
                }
            }
            catch (...) {
                options.error =
                    argument +
                    " was given \"" +
                    value +
                    "\", which is not a number.";

                return options;
            }

            if (
                argument == "--threshold" &&
                options.threshold_percent <= 0.0
            ) {
                options.error =
                    "--threshold must be positive.";

                return options;
            }

            if (
                argument == "--runs" &&
                options.runs <= 0
            ) {
                options.error =
                    "--runs must be positive.";

                return options;
            }

            continue;
        }

        // A command, an argument to one (a commit hash), or a word
        // that means nothing to this tool.
        if (
            argument.size() > 1 &&
            argument[0] == '-' &&
            !is_known_command(argument)
        ) {
            options.error =
                "Unknown option \"" +
                argument +
                "\".";

            return options;
        }

        options.arguments.push_back(
            argument
        );
    }

    return options;
}


// ============================================================
// JSON output
//
// Written by hand, like the baseline it describes: the shape is
// small and fixed, and a JSON dependency would have to be built by
// both this tool and the standalone benchmark.
// ============================================================

class JsonObject
{
public:

    void string_field(
        const string& key,
        const string& value
    ) {
        add_scalar(
            key,
            "\"" + json_escape(value) + "\""
        );
    }

    void number_field(
        const string& key,
        double value
    ) {
        ostringstream text;

        text
            << fixed
            << setprecision(2)
            << value;

        add_scalar(key, text.str());
    }

    void int_field(
        const string& key,
        int value
    ) {
        add_scalar(
            key,
            to_string(value)
        );
    }

    void bool_field(
        const string& key,
        bool value
    ) {
        add_scalar(
            key,
            value ? "true" : "false"
        );
    }

    // The nested object is kept whole rather than serialized here: how
    // deep it sits in the document is not known until the document is
    // written, and it decides how far its fields are indented.
    void object_field(
        const string& key,
        JsonObject nested
    ) {
        Field field;

        field.key = key;
        field.nested_index =
            static_cast<int>(
                nested_objects.size()
            );

        fields.push_back(field);

        nested_objects.push_back(
            move(nested)
        );
    }

    string dump(
        int depth = 0
    ) const
    {
        if (
            fields.empty()
        ) {
            return "{}";
        }

        const string indent(
            static_cast<size_t>(depth) * 4,
            ' '
        );

        const string inner(
            static_cast<size_t>(depth + 1) * 4,
            ' '
        );

        string result = "{\n";

        for (
            size_t index = 0;
            index < fields.size();
            ++index
        ) {
            const Field& field =
                fields[index];

            const string value =
                field.nested_index >= 0
                    ? nested_objects[
                        static_cast<size_t>(
                            field.nested_index
                        )
                    ].dump(depth + 1)
                    : field.text;

            result +=
                inner +
                "\"" +
                field.key +
                "\": " +
                value;

            if (
                index + 1 < fields.size()
            ) {
                result += ",";
            }

            result += "\n";
        }

        result += indent + "}";

        return result;
    }

private:

    struct Field
    {
        string key;

        // Empty when the field holds a nested object, which is
        // identified by nested_index instead.
        string text;
        int nested_index = -1;
    };

    void add_scalar(
        const string& key,
        const string& text
    ) {
        Field field;

        field.key = key;
        field.text = text;

        fields.push_back(field);
    }

    vector<Field> fields;
    vector<JsonObject> nested_objects;
};


JsonObject stats_json(
    const Stats& stats
)
{
    JsonObject object;

    object.int_field("samples", stats.samples);
    object.number_field("min_us", stats.min_us);
    object.number_field("median_us", stats.median_us);
    object.number_field("mean_us", stats.mean_us);
    object.number_field("max_us", stats.max_us);
    object.number_field("stdev_us", stats.stdev_us);
    object.int_field(
        "outliers_removed",
        stats.outliers_removed
    );
    object.bool_field(
        "outlier_check_applied",
        stats.outlier_check
    );

    return object;
}


JsonObject metadata_json(
    const Baseline& baseline
)
{
    JsonObject object;

    object.string_field("os", baseline.os);
    object.string_field("cpu", baseline.cpu);
    object.int_field("cpu_count", baseline.cpu_count);
    object.string_field("compiler", baseline.compiler);
    object.string_field("build_type", baseline.build_type);
    object.string_field(
        "cmake_generator",
        baseline.cmake_generator
    );
    object.string_field("timestamp", baseline.timestamp);
    object.string_field(
        "perfguard_version",
        baseline.perfguard_version
    );

    return object;
}


// Current environment, taken from this build. The measurements are
// filled in by the caller.
Baseline environment_baseline()
{
    Baseline baseline;

    fill_environment_metadata(baseline);

    return baseline;
}


// With --json, stdout carries the document alone, so everything a
// mode narrates is sent to stderr instead: still visible on a
// terminal, never mixed into the stream a CI job parses.
class HumanOutputGuard
{
public:

    explicit HumanOutputGuard(
        bool redirect
    )
    {
        if (
            redirect
        ) {
            saved =
                cout.rdbuf(
                    cerr.rdbuf()
                );
        }
    }

    // Called just before the document is written.
    void restore()
    {
        if (
            saved != nullptr
        ) {
            cout.rdbuf(saved);

            saved = nullptr;
        }
    }

    ~HumanOutputGuard()
    {
        restore();
    }

private:

    streambuf* saved = nullptr;
};


// Emits a JSON object and reports whether anything was written.
bool emit_json(
    HumanOutputGuard& guard,
    const JsonObject& object
)
{
    guard.restore();

    cout
        << object.dump()
        << "\n";

    return true;
}


// The error document goes through the same guard a success one does.
// With --json the guard has stdout pointed at stderr for the duration
// of the narration, so writing the document without restoring it would
// put it on the wrong stream and leave stdout empty for the caller
// that asked for a document there.
int fail_with_json(
    HumanOutputGuard& guard,
    const Options& options,
    const string& command,
    const string& message
)
{
    if (
        options.json
    ) {
        JsonObject object;

        object.string_field("command", command);
        object.string_field("status", "error");
        object.string_field("message", message);

        emit_json(guard, object);
    }

    return 2;
}


// ============================================================
// baseline
//
// Measures the working tree and stores the result, together with the
// environment it was measured in.
// ============================================================

int run_baseline_mode(
    const Options& options,
    const fs::path& benchmark_executable
)
{
    HumanOutputGuard guard(options.json);

    const fs::path project_root =
        find_project_root();

    if (
        project_root.empty()
    ) {
        cerr
            << "ERROR: Unable to find the PerfGuard project root. "
               "Make sure the command is executed inside "
               "the PerfGuard project directory.\n";

        return fail_with_json(
            guard,
            options,
            "baseline",
            "Unable to find the PerfGuard project root."
        );
    }

    const int samples =
        options.runs > 0
            ? options.runs
            : BASELINE_SAMPLES;

    cout
        << "Measuring "
        << samples
        << " samples of the working tree.\n";

    Stats stats;
    string error;

    if (
        !measure(
            benchmark_executable,
            samples,
            1,
            stats,
            error
        )
    ) {
        cerr
            << "ERROR: "
            << error
            << "\n";

        return fail_with_json(
            guard,
            options,
            "baseline",
            error
        );
    }

    Baseline baseline =
        environment_baseline();

    baseline.benchmark =
        BENCHMARK_NAME;

    baseline.commit =
        get_git_commit();

    baseline.average_us =
        stats.mean_us;

    baseline.median_us =
        stats.median_us;

    baseline.stdev_us =
        stats.stdev_us;

    baseline.min_us =
        stats.min_us;

    baseline.max_us =
        stats.max_us;

    baseline.runs =
        stats.samples;

    baseline.warmup_runs = 1;

    if (
        baseline.commit.empty()
    ) {
        cerr
            << "ERROR: Could not read the current commit.\n";

        return fail_with_json(
            guard,
            options,
            "baseline",
            "Could not read the current commit."
        );
    }

    if (
        !write_baseline(
            project_root,
            baseline,
            error
        )
    ) {
        cerr
            << "ERROR: "
            << error
            << "\n";

        return fail_with_json(
            guard,
            options,
            "baseline",
            error
        );
    }

    if (
        options.json
    ) {
        JsonObject object;

        object.string_field("command", "baseline");
        object.string_field("status", "ok");
        object.string_field("benchmark", baseline.benchmark);
        object.string_field("commit", baseline.commit);
        object.number_field("median_us", baseline.median_us);
        object.number_field("mean_us", baseline.average_us);
        object.number_field("stdev_us", baseline.stdev_us);
        object.int_field("runs", baseline.runs);
        object.object_field(
            "stats",
            stats_json(stats)
        );
        object.object_field(
            "metadata",
            metadata_json(baseline)
        );

        emit_json(guard, object);

        return 0;
    }

    cout
        << "\nBaseline created successfully.\n";

    cout
        << "File: "
        << BASELINE_PATH
        << "\n";

    cout
        << "Commit: "
        << baseline.commit
        << "\n";

    print_stats("Baseline", stats);

    return 0;
}


// ============================================================
// --check
//
// Both sides are built and measured inside this one run.
//
// The stored measurement was taken in a build directory that no
// longer exists, and identical source built in two different
// directories has been measured several percent apart. That is
// enough to decide the verdict when the slowdown being looked for
// is itself only about ten percent, so the stored number is used
// only for the commit it names; both figures come from this run.
//
// Exit codes: 0 = no regression, 1 = regression, 2 = error.
// ============================================================

int run_check_mode(
    const Options& options
)
{
    HumanOutputGuard guard(options.json);

    Baseline baseline;
    string message;
    int exit_code = 2;

    if (
        !load_comparable_baseline(
            baseline,
            message,
            exit_code
        )
    ) {
        cerr
            << "ERROR: "
            << message
            << "\n";

        return fail_with_json(
            guard,
            options,
            "check",
            message
        );
    }

    const string baseline_commit =
        baseline.commit;

    if (
        !is_valid_commit_hash(
            baseline_commit
        )
    ) {
        cerr
            << "ERROR: The baseline names \""
            << baseline_commit
            << "\", which is not a commit hash.\n";

        return fail_with_json(
            guard,
            options,
            "check",
            "The baseline names a commit that is not a commit hash."
        );
    }

    const string current_commit =
        get_git_commit();

    if (
        current_commit.empty()
    ) {
        cerr
            << "ERROR: Could not read the current commit.\n";

        return fail_with_json(
            guard,
            options,
            "check",
            "Could not read the current commit."
        );
    }

    cout
        << "\nPerfGuard Check\n";

    cout
        << "===============\n";

    cout
        << "Baseline Commit: "
        << baseline_commit
        << "\n";

    cout
        << "Current Commit:  "
        << current_commit
        << "\n";

    // --------------------------------------------------------
    // Build both sides
    // --------------------------------------------------------

    string error;

    cout
        << "\nBuilding the baseline commit...\n";

    BuildArtifacts baseline_build;

    if (
        !prepare_build(
            baseline_commit,
            baseline_build,
            error
        )
    ) {
        cerr
            << "\nERROR: "
            << error
            << "\n";

        if (
            !baseline_build.build_log.empty()
        ) {
            cerr
                << baseline_build.build_log;
        }

        release_build(baseline_build);

        return fail_with_json(
            guard,
            options,
            "check",
            error
        );
    }

    cout
        << "Building the current commit...\n";

    BuildArtifacts current_build;

    if (
        !prepare_build(
            current_commit,
            current_build,
            error
        )
    ) {
        cerr
            << "\nERROR: "
            << error
            << "\n";

        if (
            !current_build.build_log.empty()
        ) {
            cerr
                << current_build.build_log;
        }

        release_build(
            baseline_build
        );

        release_build(
            current_build
        );

        return fail_with_json(
            guard,
            options,
            "check",
            error
        );
    }

    // --------------------------------------------------------
    // Measure both, alternating
    // --------------------------------------------------------

    const int samples_per_side =
        options.runs > 0
            ? options.runs
            : CHECK_SAMPLES_PER_SIDE;

    cout
        << "\nMeasuring "
        << samples_per_side
        << " samples per side, alternating.\n";

    cout
        << "Each sample runs the benchmark "
        << BENCHMARK_RUNS
        << " times, so this takes a few minutes.\n";

    Stats baseline_stats;
    Stats current_stats;

    const bool measured =
        measure_interleaved(
            baseline_build.executable,
            current_build.executable,
            samples_per_side,
            1,
            baseline_stats,
            current_stats,
            error
        );

    release_build(
        current_build
    );

    release_build(
        baseline_build
    );

    if (
        !measured
    ) {
        cerr
            << "\nERROR: "
            << error
            << "\n";

        return fail_with_json(
            guard,
            options,
            "check",
            error
        );
    }

    // --------------------------------------------------------
    // Verdict
    // --------------------------------------------------------

    const double change =
        calculate_change(
            baseline_stats.median_us,
            current_stats.median_us
        );

    const bool regression =
        change >
        options.threshold_percent;

    const Baseline environment =
        environment_baseline();

    if (
        options.json
    ) {
        JsonObject object;

        object.string_field(
            "command",
            "check"
        );
        object.string_field(
            "status",
            regression
                ? "regression"
                : "ok"
        );
        object.string_field(
            "baseline_commit",
            baseline_commit
        );
        object.string_field(
            "current_commit",
            current_commit
        );
        object.number_field(
            "baseline_median_us",
            baseline_stats.median_us
        );
        object.number_field(
            "current_median_us",
            current_stats.median_us
        );
        object.number_field(
            "change_percent",
            change
        );
        object.number_field(
            "threshold_percent",
            options.threshold_percent
        );

        JsonObject stats;

        stats.object_field(
            "baseline",
            stats_json(baseline_stats)
        );
        stats.object_field(
            "current",
            stats_json(current_stats)
        );

        object.object_field("stats", stats);
        object.object_field(
            "metadata",
            metadata_json(environment)
        );

        emit_json(guard, object);

        return regression ? 1 : 0;
    }

    cout
        << "\nBaseline Median: "
        << fixed
        << setprecision(2)
        << baseline_stats.median_us
        << " us\n";

    cout
        << "Current Median:  "
        << fixed
        << setprecision(2)
        << current_stats.median_us
        << " us\n";

    cout
        << "Change:          ";

    if (
        change >= 0
    ) {
        cout
            << "+";
    }

    cout
        << fixed
        << setprecision(2)
        << change
        << "%\n";

    cout
        << "Threshold:       "
        << options.threshold_percent
        << "%\n";

    print_stats(
        "Baseline",
        baseline_stats
    );

    print_stats(
        "Current",
        current_stats
    );

    cout
        << "\n";

    if (
        regression
    ) {
        cout
            << "Result: REGRESSION\n";

        return 1;
    }

    cout
        << "Result: OK\n";

    return 0;
}


// ============================================================
// Working-tree measurement
//
// The figure a fresh run of the working tree reports. Measured the
// way a baseline is, so the two are described by the same statistic
// and can be compared directly.
// ============================================================

bool measure_working_tree(
    const Options& options,
    const fs::path& benchmark_executable,
    Stats& stats,
    string& error
)
{
    const int samples =
        options.runs > 0
            ? options.runs
            : BASELINE_SAMPLES;

    return measure(
        benchmark_executable,
        samples,
        1,
        stats,
        error
    );
}


// ============================================================
// Main
// ============================================================

int main(
    int argc,
    char* argv[]
)
{
    // Resolve the benchmark next to the running PerfGuard executable.
    const fs::path perfguard_executable =
        fs::absolute(argv[0]);

    const fs::path benchmark_executable =
        get_benchmark_executable(perfguard_executable);

    const Options options =
        parse_options(argc, argv);

    if (
        !options.error.empty()
    ) {
        cerr
            << "ERROR: "
            << options.error
            << "\n\n";

        cout
            << usage_text();

        return 2;
    }

    if (
        options.help
    ) {
        cout
            << usage_text();

        return 0;
    }

    if (
        options.version
    ) {
        cout
            << "PerfGuard "
            << PERFGUARD_VERSION
            << "\n";

        return 0;
    }

    // A command given too few arguments must say so. Without this it
    // would match no branch below and fall through to the plain
    // benchmark run, measuring the working tree as though that were
    // what was asked for.
    if (
        !options.arguments.empty()
    ) {
        const string command =
            options.arguments[0];

        if (
            is_known_command(command) &&
            static_cast<int>(
                options.arguments.size()
            ) < required_arguments(command)
        ) {
            cerr
                << "ERROR: "
                << command
                << " needs "
                << (
                    command == "--bisect-test"
                        ? "a baseline and a threshold."
                        : "a commit."
                )
                << "\n\n";

            cout
                << usage_text();

            return 2;
        }
    }

    // ========================================================
    // --analyze
    //
    // Full automatic performance analysis:
    //
    //   1. Run current benchmark
    //   2. Detect regression
    //   3. Automatically bisect Git history
    //   4. Diagnose first bad commit
    //   5. Generate Markdown report
    //
    // Exit codes:
    //
    //   0 = no regression
    //   1 = regression detected
    //   2 = a step failed, or the stored baseline cannot be compared
    //
    // A detected regression is an answer; a failed step is not, so
    // they do not share a code. Matches --check, --report and
    // --simulate-regression.
    // ========================================================

    if (
        options.arguments.size() >= 1 &&
        options.arguments[0] ==
            "--analyze"
    )
    {
        cout
            << "========================================\n";

        cout
            << "PerfGuard Automatic Performance Analysis\n";

        cout
            << "========================================\n\n";


        // ----------------------------------------------------
        // Step 1: Check current benchmark
        // ----------------------------------------------------

        cout
            << "[1/4] Detecting performance regression...\n\n";

        Baseline stored_baseline;
        string baseline_message;

        int baseline_exit_code = 2;

        if (
            !load_comparable_baseline(
                stored_baseline,
                baseline_message,
                baseline_exit_code
            )
        ) {
            cerr
                << "ERROR: "
                << baseline_message
                << "\n";

            return baseline_exit_code;
        }

        const double baseline =
            stored_baseline.median_us;

        const string baseline_commit =
            stored_baseline.commit;

        Stats measurement;

        string measurement_error;

        if (
            !measure_working_tree(
                options,
                benchmark_executable,
                measurement,
                measurement_error
            )
        ) {
            cerr
                << "ERROR: "
                << measurement_error
                << "\n";

            return 2;
        }

        const double current =
            measurement.median_us;

        double change =
            calculate_change(
                baseline,
                current
            );

        cout
            << "\nBaseline: "
            << fixed
            << setprecision(2)
            << baseline
            << " us\n";

        cout
            << "Current:  "
            << fixed
            << setprecision(2)
            << current
            << " us\n";

        cout
            << "Change:   ";

        if (
            change >= 0
        ) {
            cout
                << "+";
        }

        cout
            << fixed
            << setprecision(2)
            << change
            << "%\n";

        cout
            << "Threshold: "
            << options.threshold_percent
            << "%\n";


        // ----------------------------------------------------
        // No regression
        // ----------------------------------------------------

        if (
            change <=
            options.threshold_percent
        ) {
            cout
                << "\nResult: OK\n";

            cout
                << "\nNo performance regression detected.\n";

            return 0;
        }


        cout
            << "\nResult: REGRESSION\n\n";


        // ----------------------------------------------------
        // Step 2: Automatic bisect
        // ----------------------------------------------------

        cout
            << "[2/4] Finding first bad commit...\n\n";

        BisectResult bisect_result =
            run_bisect(
                perfguard_executable.string(),
                options.force,
                options.threshold_percent
            );

        if (
            !bisect_result.output.empty()
        ) {
            cout
                << bisect_result.output;
        }

        if (
            !bisect_result.success
        ) {
            cerr
                << "\nBisect failed: "
                << bisect_result.error
                << "\n";

            return 2;
        }

        const string first_bad_commit =
            bisect_result.first_bad_commit;

        if (
            first_bad_commit.empty()
        ) {
            cerr
                << "\nBisect did not return "
                   "a first bad commit.\n";

            return 2;
        }

        cout
            << "\nFirst bad commit: "
            << first_bad_commit
            << "\n\n";


        // ----------------------------------------------------
        // Step 3: Diagnose
        // ----------------------------------------------------

        cout
            << "[3/4] Diagnosing first bad commit...\n\n";

        EvidenceReport evidence =
            analyze_commit(
                first_bad_commit
            );

        if (
            !evidence.success
        ) {
            cerr
                << "Evidence analysis failed: "
                << evidence.error
                << "\n";

            return 2;
        }

        DiagnosisResult diagnosis =
            diagnose_commit(
                first_bad_commit,
                options.runs > 0 ? options.runs : BASELINE_SAMPLES,
                1,
                options.threshold_percent
            );

        if (
            !diagnosis.success
        ) {
            cerr
                << "Commit diagnosis failed: "
                << diagnosis.error
                << "\n";

            if (
                !diagnosis.build_log.empty()
            ) {
                cerr
                    << "\n--- Build output ---\n"
                    << diagnosis.build_log
                    << "\n--------------------\n";
            }

            return 2;
        }


        // ----------------------------------------------------
        // Step 4: Generate report
        // ----------------------------------------------------

        cout
            << "[4/4] Generating report...\n\n";

        ofstream report(
            "perfguard-report.md"
        );

        if (
            !report
        ) {
            cerr
                << "ERROR: Could not create "
                   "perfguard-report.md\n";

            return 2;
        }

        report
            << "# PerfGuard Performance Report\n\n";

        report
            << "## Summary\n\n";

        report
            << "**Performance regression detected.**\n\n";

        report
            << "## Benchmark\n\n";

        report
            << "- Name: `sort_1m`\n";

        report
            << "- Runs: "
            << BENCHMARK_RUNS
            << "\n\n";

        report
            << "## Performance\n\n";

        report
            << "| Metric | Value |\n";

        report
            << "|---|---:|\n";

        report
            << "| Baseline | "
            << fixed
            << setprecision(2)
            << baseline
            << " us |\n";

        report
            << "| Current | "
            << fixed
            << setprecision(2)
            << current
            << " us |\n";

        report
            << "| Change | +"
            << fixed
            << setprecision(2)
            << change
            << "% |\n";

        report
            << "| Threshold | "
            << options.threshold_percent
            << "% |\n\n";


        // ----------------------------------------------------
        // Git information
        // ----------------------------------------------------

        report
            << "## Git\n\n";

        report
            << "- Baseline commit: `"
            << baseline_commit
            << "`\n";

        report
            << "- First bad commit: `"
            << first_bad_commit
            << "`\n\n";


        // ----------------------------------------------------
        // Commit information
        // ----------------------------------------------------

        report
            << "## First Bad Commit\n\n";

        report
            << "- Commit: `"
            << evidence.commit
            << "`\n";

        report
            << "- Message: "
            << evidence.message
            << "\n";

        report
            << "- Author: "
            << evidence.author
            << "\n\n";


        // ----------------------------------------------------
        // Changed files
        // ----------------------------------------------------

        report
            << "## Changed Files\n\n";

        for (
            const auto& file :
            evidence.changed_files
        ) {
            report
                << "- `"
                << file
                << "`\n";
        }

        report
            << "\n";


        // ----------------------------------------------------
        // Change summary
        // ----------------------------------------------------

        report
            << "## Change Summary\n\n";

        report
            << "- Lines added: "
            << evidence.lines_added
            << "\n";

        report
            << "- Lines removed: "
            << evidence.lines_removed
            << "\n\n";


        // ----------------------------------------------------
        // Performance evidence
        // ----------------------------------------------------

        report
            << "## Performance Evidence\n\n";

        if (
            evidence.performance_evidence.empty()
        ) {
            report
                << "No obvious performance-related "
                   "changes were detected.\n\n";
        }
        else {
            for (
                const auto& line :
                evidence.performance_evidence
            ) {
                report
                    << "```cpp\n";

                report
                    << line
                    << "\n";

                report
                    << "```\n\n";
            }
        }


        // ----------------------------------------------------
        // Diagnosis
        // ----------------------------------------------------

        report
            << "## Diagnosis\n\n";

        if (
            diagnosis.regression
        ) {
            report
                << "Performance regression detected.\n\n";
        }
        else {
            report
                << "No significant performance regression "
                   "detected at the target commit.\n\n";
        }

        report
            << "- Baseline: "
            << fixed
            << setprecision(2)
            << diagnosis.baseline_us
            << " us\n";

        report
            << "- Target: "
            << fixed
            << setprecision(2)
            << diagnosis.current_us
            << " us\n";

        report
            << "- Change: "
            << fixed
            << setprecision(2)
            << diagnosis.change_percent
            << "%\n";

        report
            << "- Threshold: "
            << fixed
            << setprecision(2)
            << diagnosis.threshold_percent
            << "%\n\n";


        // ----------------------------------------------------
        // Result
        // ----------------------------------------------------

        report
            << "## Result\n\n";

        report
            << "`REGRESSION`\n";

        report.close();


        // ----------------------------------------------------
        // Final console output
        // ----------------------------------------------------

        cout
            << "========================================\n";

        cout
            << "Analysis Complete\n";

        cout
            << "========================================\n\n";

        cout
            << "First bad commit:\n"
            << "  "
            << first_bad_commit
            << "\n\n";

        cout
            << "Regression:\n"
            << "  +"
            << fixed
            << setprecision(2)
            << change
            << "%\n\n";

        cout
            << "Report:\n"
            << "  perfguard-report.md\n";

        return 1;
    }
    // ========================================================
    // --report
    //
    // Generate a Markdown performance regression report.
    // ========================================================

    if (
        options.arguments.size() >= 1 &&
        options.arguments[0] ==
            "--report"
    )
    {
        Baseline stored_baseline;
        string baseline_message;

        int baseline_exit_code = 2;

        if (
            !load_comparable_baseline(
                stored_baseline,
                baseline_message,
                baseline_exit_code
            )
        ) {
            cerr
                << "ERROR: "
                << baseline_message
                << "\n";

            return baseline_exit_code;
        }

        const double baseline =
            stored_baseline.median_us;

        const string baseline_commit =
            stored_baseline.commit;

        string current_commit =
            get_git_commit();

        if (
            current_commit.empty()
        ) {
            current_commit =
                "unknown";
        }

        cout
            << "========================================\n";

        cout
            << "PerfGuard Performance Report\n";

        cout
            << "========================================\n";

        cout
            << "\nMeasuring the working tree...\n";

        Stats measurement;

        string measurement_error;

        if (
            !measure_working_tree(
                options,
                benchmark_executable,
                measurement,
                measurement_error
            )
        ) {
            cerr
                << "ERROR: "
                << measurement_error
                << "\n";

            return 2;
        }

        const double current =
            measurement.median_us;

        double change =
            calculate_change(
                baseline,
                current
            );

        bool regression =
            change >
            options.threshold_percent;

        // ----------------------------------------------------
        // Analyze current commit
        // ----------------------------------------------------

        EvidenceReport evidence =
            analyze_commit(
                current_commit
            );

        // ----------------------------------------------------
        // Generate Markdown
        // ----------------------------------------------------

        ofstream report(
            "perfguard-report.md"
        );

        if (
            !report
        ) {
            cerr
                << "ERROR: Could not create "
                   "perfguard-report.md\n";

            return 2;
        }

        report
            << "# PerfGuard Performance Report\n\n";

        report
            << "## Summary\n\n";

        if (
            regression
        ) {
            report
                << "**Performance regression detected.**\n\n";
        }
        else {
            report
                << "**No significant performance regression detected.**\n\n";
        }

        report
            << "## Benchmark\n\n";

        report
            << "- Name: `sort_1m`\n";

        report
            << "- Runs: "
            << BENCHMARK_RUNS
            << "\n\n";

        report
            << "## Performance\n\n";

        report
            << "| Metric | Value |\n";

        report
            << "|---|---:|\n";

        report
            << "| Baseline | "
            << fixed
            << setprecision(2)
            << baseline
            << " us |\n";

        report
            << "| Current | "
            << fixed
            << setprecision(2)
            << current
            << " us |\n";

        report
            << "| Change | ";

        if (
            change >= 0
        ) {
            report
                << "+";
        }

        report
            << fixed
            << setprecision(2)
            << change
            << "% |\n";

        report
            << "| Threshold | "
            << options.threshold_percent
            << "% |\n\n";

        report
            << "## Git\n\n";

        report
            << "- Baseline commit: `"
            << baseline_commit
            << "`\n";

        report
            << "- Current commit: `"
            << current_commit
            << "`\n\n";

        // ----------------------------------------------------
        // Commit evidence
        // ----------------------------------------------------

        if (
            evidence.success
        ) {
            report
                << "## Changed Files\n\n";

            if (
                evidence.changed_files.empty()
            ) {
                report
                    << "No changed files detected.\n\n";
            }
            else {
                for (
                    const auto& file :
                    evidence.changed_files
                ) {
                    report
                        << "- `"
                        << file
                        << "`\n";
                }

                report
                    << "\n";
            }

            report
                << "## Change Summary\n\n";

            report
                << "- Lines added: "
                << evidence.lines_added
                << "\n";

            report
                << "- Lines removed: "
                << evidence.lines_removed
                << "\n\n";

            report
                << "## Performance Evidence\n\n";

            if (
                evidence.performance_evidence.empty()
            ) {
                report
                    << "No obvious performance-related "
                       "changes were detected.\n\n";
            }
            else {
                for (
                    const auto& line :
                    evidence.performance_evidence
                ) {
                    report
                        << "```cpp\n"
                        << line
                        << "\n"
                        << "```\n\n";
                }
            }
        }
        else {
            report
                << "## Commit Evidence\n\n";

            report
                << "Evidence analysis failed: "
                << evidence.error
                << "\n\n";
        }

        // ----------------------------------------------------
        // Diagnosis
        // ----------------------------------------------------

        report
            << "## Diagnosis\n\n";

        if (
            regression
        ) {
            report
                << "The current benchmark result exceeds "
                   "the configured regression threshold.\n\n";

            if (
                evidence.success &&
                !evidence.performance_evidence.empty()
            ) {
                report
                    << "The commit contains changes that "
                       "may directly affect benchmark performance.\n\n";
            }
        }
        else {
            report
                << "The current benchmark result is within "
                   "the configured regression threshold.\n\n";
        }

        report
            << "## Result\n\n";

        if (
            regression
        ) {
            report
                << "`REGRESSION`\n";
        }
        else {
            report
                << "`OK`\n";
        }

        report.close();

        cout
            << "\nReport generated:\n"
            << "  perfguard-report.md\n";

        return regression
            ? 1
            : 0;
    }
    // ========================================================
    // --diagnose <commit>
    //
    // Full performance regression diagnosis.
    // ========================================================

    if (
        options.arguments.size() >= 2 &&
        options.arguments[0] ==
            "--diagnose"
    )
    {
        HumanOutputGuard guard(options.json);

        const string commit =
            options.arguments[1];

        EvidenceReport evidence =
            analyze_commit(
                commit
            );

        if (
            !evidence.success
        ) {
            cerr
                << "Evidence analysis failed: "
                << evidence.error
                << "\n";

            return fail_with_json(
                guard,
                options,
                "diagnose",
                evidence.error
            );
        }

        DiagnosisResult diagnosis =
            diagnose_commit(
                commit,
                options.runs > 0 ? options.runs : BASELINE_SAMPLES,
                1,
                options.threshold_percent
            );

        if (
            !diagnosis.success
        ) {
            cerr
                << "Commit diagnosis failed: "
                << diagnosis.error
                << "\n";

            if (
                !diagnosis.build_log.empty()
            ) {
                cerr
                    << "\n--- Build output ---\n"
                    << diagnosis.build_log
                    << "\n--------------------\n";
            }

            return fail_with_json(
                guard,
                options,
                "diagnose",
                diagnosis.error
            );
        }

        if (
            options.json
        ) {
            JsonObject object;

            object.string_field("command", "diagnose");
            object.string_field(
                "status",
                diagnosis.regression
                    ? "regression"
                    : "ok"
            );
            object.string_field(
                "commit",
                diagnosis.commit
            );
            object.number_field(
                "baseline_median_us",
                diagnosis.baseline_us
            );
            object.number_field(
                "current_median_us",
                diagnosis.current_us
            );
            object.number_field(
                "change_percent",
                diagnosis.change_percent
            );
            object.number_field(
                "threshold_percent",
                diagnosis.threshold_percent
            );
            object.object_field(
                "stats",
                stats_json(diagnosis.stats)
            );

            emit_json(guard, object);

            return diagnosis.regression ? 1 : 0;
        }

        cout
            << "\n";

        cout
            << "========================================\n";

        cout
            << "Performance Regression Diagnosis\n";

        cout
            << "========================================\n";

        cout
            << "\n";

        cout
            << "Commit: "
            << evidence.commit
            << "\n";

        cout
            << "Message: "
            << evidence.message
            << "\n";

        cout
            << "Author: "
            << evidence.author
            << "\n";

        cout
            << "\nChanged files:\n";

        for (
            const auto& file :
            evidence.changed_files
        ) {
            cout
                << "  "
                << file
                << "\n";
        }

        cout
            << "\nChange summary:\n";

        cout
            << "  Lines added:   "
            << evidence.lines_added
            << "\n";

        cout
            << "  Lines removed: "
            << evidence.lines_removed
            << "\n";

        cout
            << "\nPotential performance evidence:\n";

        if (
            evidence.performance_evidence.empty()
        ) {
            cout
                << "  None detected.\n";
        }
        else {
            for (
                const auto& line :
                evidence.performance_evidence
            ) {
                cout
                    << "  "
                    << line
                    << "\n";
            }
        }

        cout
            << "\nBenchmark impact:\n";

        cout
            << "  Baseline: "
            << diagnosis.baseline_us
            << " us\n";

        cout
            << "  Target:   "
            << diagnosis.current_us
            << " us\n";

        cout
            << "  Change:   "
            << diagnosis.change_percent
            << "%\n";

        cout
            << "  Threshold: "
            << diagnosis.threshold_percent
            << "%\n";

        cout
            << "\nDiagnosis:\n";

        if (
            diagnosis.regression
        ) {
            cout
                << "  Performance regression detected.\n";
        }
        else {
            cout
                << "  No significant performance regression detected.\n";
        }

        cout
            << "\n";

        return diagnosis.regression
            ? 1
            : 0;
    }


    // ========================================================
    // --evidence <commit>
    //
    // Show Git evidence without running benchmark diagnosis.
    // ========================================================

    if (
        options.arguments.size() >= 2 &&
        options.arguments[0] ==
            "--evidence"
    )
    {
        return show_commit_evidence(
            options.arguments[1]
        );
    }


    // ========================================================
    // --bisect-test <baseline_us> <threshold>
    //
    // INTERNAL MODE
    //
    // This is called by the current PerfGuard executable while
    // Git checks out historical commits.
    //
    // Exit codes:
    //
    //   0   = GOOD
    //   1   = BAD / regression
    //   125 = SKIP
    // ========================================================

    if (
        options.arguments.size() >= 3 &&
        options.arguments[0] ==
            "--bisect-test"
    )
    {
        return run_bisect_test(
            options.arguments[1],
            options.arguments[2]
        );
    }


    // ========================================================
    // --bisect
    //
    // Automatically find the first commit that introduced
    // a performance regression.
    // ========================================================

    if (
        options.arguments.size() >= 1 &&
        options.arguments[0] ==
            "--bisect"
    )
    {
        const BisectResult result =
            run_bisect(
                perfguard_executable.string(),
                options.force,
                options.threshold_percent
            );

        if (
            !result.output.empty()
        ) {
            cout
                << result.output;
        }

        if (
            !result.success
        ) {
            cerr
                << "\nBisect failed: "
                << result.error
                << "\n";

            return 2;
        }

        cout
            << "\n========================================\n";

        cout
            << "First Bad Commit\n";

        cout
            << "========================================\n";

        cout
            << "Commit: "
            << result.first_bad_commit
            << "\n";

        return 0;
    }


    // ========================================================
    // --simulate-regression
    //
    // Artificially creates a slowdown.
    //
    // This does NOT modify the baseline.
    // ========================================================

    if (
        options.arguments.size() >= 1 &&
        options.arguments[0] ==
            "--simulate-regression"
    )
    {
        cout
            << "================================\n";

        cout
            << "PerfGuard Regression Test\n";

        cout
            << "================================\n";

        cout
            << "Running artificial regression...\n";

        // The fixture provides its own reference: the same workload is
        // timed once as written and once with the slowdown injected,
        // on this machine, in this run.
        double baseline =
            run_artificial_regression_benchmark(
                0
            );

        double average =
            run_artificial_regression_benchmark(
                FIXTURE_SLEEP_MS
            );

        cout
            << "\n";

        print_benchmark_result(
            average
        );

        double change =
            calculate_change(
                baseline,
                average
            );

        cout
            << "Baseline: "
            << fixed
            << setprecision(2)
            << baseline
            << " us\n";

        cout
            << "Current:  "
            << fixed
            << setprecision(2)
            << average
            << " us\n";

        cout
            << "Change:   ";

        if (
            change >= 0
        ) {
            cout
                << "+";
        }

        cout
            << fixed
            << setprecision(2)
            << change
            << "%\n";

        cout
            << "Threshold: "
            << options.threshold_percent
            << "%\n";

        if (
            change >
            options.threshold_percent
        ) {
            cout
                << "Result: REGRESSION\n";

            return 1;
        }

        cout
            << "Result: OK\n";

        return 0;
    }


    // ========================================================
    // baseline
    //
    // Measures the working tree rather than any commit, so it runs
    // before the benchmark below and replaces it.
    // ========================================================

    if (
        options.arguments.size() >= 1 &&
        options.arguments[0] ==
            "baseline"
    ) {
        return run_baseline_mode(
            options,
            benchmark_executable
        );
    }


    // ========================================================
    // --check
    //
    // Runs its own builds, so it must not fall through to the
    // benchmark of the working tree below.
    // ========================================================

    if (
        options.arguments.size() >= 1 &&
        options.arguments[0] ==
            "--check"
    ) {
        return run_check_mode(options);
    }


    // ========================================================
    // Plain run: measure the working tree and compare it against the
    // stored baseline.
    // ========================================================

    HumanOutputGuard guard(options.json);

    Baseline stored_baseline;
    string baseline_message;

    int baseline_exit_code = 2;

    if (
        !load_comparable_baseline(
            stored_baseline,
            baseline_message,
            baseline_exit_code
        )
    ) {
        cerr
            << "ERROR: "
            << baseline_message
            << "\n";

        return fail_with_json(
            guard,
            options,
            "benchmark",
            baseline_message
        );
    }

    string git_commit =
        get_git_commit();

    if (
        git_commit.empty()
    ) {
        git_commit =
            "unknown";
    }

    cout
        << "================================\n";

    cout
        << "PerfGuard Performance Benchmark\n";

    cout
        << "================================\n";

    cout
        << "Commit: "
        << git_commit
        << "\n\n";

    cout
        << "Measuring the working tree...\n";

    Stats measurement;

    string measurement_error;

    if (
        !measure_working_tree(
            options,
            benchmark_executable,
            measurement,
            measurement_error
        )
    ) {
        cerr
            << "ERROR: "
            << measurement_error
            << "\n";

        return fail_with_json(
            guard,
            options,
            "benchmark",
            measurement_error
        );
    }

    const double baseline =
        stored_baseline.median_us;

    const double current =
        measurement.median_us;

    const double change =
        calculate_change(
            baseline,
            current
        );

    const bool regression =
        change >
        options.threshold_percent;

    const Baseline environment =
        environment_baseline();

    if (
        options.json
    ) {
        JsonObject object;

        object.string_field("command", "benchmark");
        object.string_field(
            "status",
            regression
                ? "regression"
                : "ok"
        );
        object.string_field(
            "baseline_commit",
            stored_baseline.commit
        );
        object.string_field(
            "current_commit",
            git_commit
        );
        object.number_field(
            "baseline_median_us",
            baseline
        );
        object.number_field(
            "current_median_us",
            current
        );
        object.number_field("change_percent", change);
        object.number_field(
            "threshold_percent",
            options.threshold_percent
        );
        object.object_field(
            "stats",
            stats_json(measurement)
        );
        object.object_field(
            "metadata",
            metadata_json(environment)
        );

        emit_json(guard, object);

        return regression ? 1 : 0;
    }

    cout
        << "\n";

    cout
        << "Baseline Commit: "
        << stored_baseline.commit
        << "\n";

    cout
        << "Current Commit:  "
        << git_commit
        << "\n";

    cout
        << "Baseline Median: "
        << fixed
        << setprecision(2)
        << baseline
        << " us\n";

    cout
        << "Current Median:  "
        << fixed
        << setprecision(2)
        << current
        << " us\n";

    cout
        << "Performance Change: ";

    if (
        change >= 0
    ) {
        cout
            << "+";
    }

    cout
        << fixed
        << setprecision(2)
        << change
        << "%\n";

    cout
        << "Threshold: "
        << options.threshold_percent
        << "%\n";

    print_stats(
        "Current",
        measurement
    );

    cout
        << "\n";

    if (
        regression
    ) {
        cout
            << "WARNING: Performance Regression!\n";

        return 1;
    }

    cout
        << "Performance OK.\n";

    return 0;
}