// Dependency-free tests for the pure helpers in src/core.cpp.
//
// No third-party framework: the project builds with nothing but
// CMake and MSVC, and adding a test dependency to get assert
// macros is not worth it.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "core.h"
#include "evidence.h"
#include "measurement.h"
#include "perfguard_config.h"

using namespace std;
using namespace perfguard;

namespace fs = std::filesystem;

namespace {

int checks = 0;
int failures = 0;

void check(
    bool condition,
    const string& name
)
{
    ++checks;

    if (condition) {
        return;
    }

    ++failures;

    printf(
        "FAIL: %s\n",
        name.c_str()
    );
}

void write_text_file(
    const fs::path& path,
    const string& contents
)
{
    ofstream file(path);

    file << contents;
}

string read_text_file(
    const fs::path& path
)
{
    ifstream file(path);

    if (!file) {
        return string();
    }

    ostringstream contents;

    contents << file.rdbuf();

    return contents.str();
}

void check_equal(
    const string& actual,
    const string& expected,
    const string& name
)
{
    check(
        actual == expected,
        name +
            " (expected \"" +
            expected +
            "\", got \"" +
            actual +
            "\")"
    );
}

void check_near(
    double actual,
    double expected,
    const string& name
)
{
    check(
        fabs(
            actual - expected
        ) < 1e-9,
        name
    );
}

// ============================================================
// trim
// ============================================================

void test_trim()
{
    check_equal(
        trim("  abc  "),
        "abc",
        "trim removes surrounding spaces"
    );

    check_equal(
        trim("abc\r\n"),
        "abc",
        "trim removes trailing CRLF"
    );

    check_equal(
        trim("\t abc \n"),
        "abc",
        "trim removes tabs and newline"
    );

    check_equal(
        trim(""),
        "",
        "trim of empty string"
    );

    check_equal(
        trim("   "),
        "",
        "trim of whitespace-only string"
    );

    check_equal(
        trim("a b"),
        "a b",
        "trim keeps inner spaces"
    );
}

// ============================================================
// is_valid_commit_hash
// ============================================================

void test_is_valid_commit_hash()
{
    check(
        is_valid_commit_hash("41f6091"),
        "accepts 7 hex characters"
    );

    check(
        !is_valid_commit_hash("41f609"),
        "rejects 6 hex characters"
    );

    check(
        is_valid_commit_hash("41F6091"),
        "accepts uppercase hex"
    );

    check(
        is_valid_commit_hash(
            "41f6091a2b3c4d5e6f708192a3b4c5d6e7f80912"
        ),
        "accepts a full 40 character hash"
    );

    check(
        !is_valid_commit_hash(
            "41f6091a2b3c4d5e6f708192a3b4c5d6e7f80912"
            "41f6091a2b3c4d5e6f708192a3b4c5d6e7f8091234"
        ),
        "rejects a hash longer than 64 characters"
    );

    check(
        !is_valid_commit_hash(""),
        "rejects an empty string"
    );

    check(
        !is_valid_commit_hash("xyz1234"),
        "rejects non-hex characters"
    );

    check(
        !is_valid_commit_hash("41f6091 "),
        "rejects a trailing space"
    );

    check(
        !is_valid_commit_hash("HEAD"),
        "rejects a ref name"
    );
}

// ============================================================
// parse_average
// ============================================================

void test_parse_average()
{
    double value = 0.0;

    check(
        parse_average(
            "Average: 123.45 us\n",
            value
        ),
        "parses a decimal average"
    );

    check_near(
        value,
        123.45,
        "parsed decimal average value"
    );

    check(
        parse_average(
            "Average: 200 us\n",
            value
        )
        &&
        value == 200.0,
        "parses an average without decimals"
    );

    check(
        parse_average(
            "Benchmark: sort_1m\n"
            "Runs: 10\n"
            "Average: 190498.5 us\n",
            value
        )
        &&
        value == 190498.5,
        "finds the average among other lines"
    );

    check(
        !parse_average(
            "Benchmark: sort_1m\n",
            value
        ),
        "fails when there is no average line"
    );

    check(
        !parse_average(
            "Average: abc us\n",
            value
        ),
        "fails on a non-numeric average"
    );

    check(
        !parse_average(
            "",
            value
        ),
        "fails on empty output"
    );
}

// ============================================================
// calculate_change
// ============================================================

void test_calculate_change()
{
    check_near(
        calculate_change(100.0, 110.0),
        10.0,
        "positive change"
    );

    check_near(
        calculate_change(100.0, 90.0),
        -10.0,
        "negative change"
    );

    check_near(
        calculate_change(100.0, 100.0),
        0.0,
        "no change"
    );

    check_near(
        calculate_change(0.0, 100.0),
        0.0,
        "guards against a zero baseline"
    );

    check_near(
        calculate_change(-5.0, 100.0),
        0.0,
        "guards against a negative baseline"
    );
}

// ============================================================
// extract_first_bad_commit
// ============================================================

void test_extract_first_bad_commit()
{
    check_equal(
        extract_first_bad_commit(
            "04521bc1234567890 is the first bad commit\n"
        ),
        "04521bc1234567890",
        "extracts the first bad commit"
    );

    check_equal(
        extract_first_bad_commit(
            "bisect: some earlier line\n"
            "abc1234 is the first bad commit\n"
            "commit abc1234\n"
        ),
        "abc1234",
        "extracts it from surrounding output"
    );

    check_equal(
        extract_first_bad_commit(
            "no result here\n"
        ),
        "",
        "returns empty when git reports no bad commit"
    );
}

// ============================================================
// analyze_added_line
// ============================================================

void test_analyze_added_line()
{
    string evidence;

    check(
        analyze_added_line(
            "+        std::sort(data.begin(), data.end());",
            evidence
        ),
        "flags an added sort call"
    );

    check_equal(
        evidence,
        "+        std::sort(data.begin(), data.end());",
        "returns the matched line as evidence"
    );

    check(
        analyze_added_line(
            "+        this_thread::sleep_for(",
            evidence
        ),
        "flags an added sleep"
    );

    check(
        !analyze_added_line(
            "+++ b/src/main.cpp",
            evidence
        ),
        "ignores the diff file header"
    );

    check(
        !analyze_added_line(
            "-        std::sort(data.begin(), data.end());",
            evidence
        ),
        "ignores a removed line"
    );

    check(
        !analyze_added_line(
            "+        int index = 0;",
            evidence
        ),
        "ignores an irrelevant added line"
    );

    check(
        !analyze_added_line(
            "",
            evidence
        ),
        "ignores an empty line"
    );
}

// ============================================================
// aggregate
// ============================================================

void test_aggregate()
{
    const Stats empty =
        aggregate({});

    check(
        empty.samples == 0,
        "aggregate of no samples reports none"
    );

    const Stats single =
        aggregate({100.0});

    check(
        single.samples == 1 &&
        single.median_us == 100.0 &&
        single.min_us == 100.0 &&
        single.max_us == 100.0 &&
        single.stdev_us == 0.0,
        "a single sample is its own median, min and max"
    );

    check(
        !single.outlier_check,
        "outlier rejection is skipped for a single sample"
    );

    const Stats odd =
        aggregate({3.0, 1.0, 2.0});

    check(
        odd.median_us == 2.0,
        "median of an odd sample set"
    );

    check(
        odd.mean_us == 2.0,
        "mean of an odd sample set"
    );

    const Stats even =
        aggregate({1.0, 2.0, 3.0, 4.0});

    check(
        even.median_us == 2.5,
        "median of an even sample set averages the middle pair"
    );

    check(
        even.mean_us == 2.5,
        "mean of an even sample set"
    );

    // The mean follows a single slow sample; the median does not.
    // Both are reported, and the median is what a verdict uses.
    const Stats skewed =
        aggregate({100.0, 100.0, 100.0, 1000.0});

    check(
        skewed.median_us == 100.0 &&
        skewed.mean_us == 325.0,
        "the mean is dragged by an outlier where the median is not"
    );

    // Six samples: below the threshold, so the outlier must stay and
    // the output has to say the criterion was not applied.
    const Stats few =
        aggregate({100.0, 101.0, 99.0, 100.0, 102.0, 1000.0});

    check(
        !few.outlier_check,
        "outlier rejection is not attempted on six samples"
    );

    check(
        few.outliers_removed == 0 &&
        few.max_us == 1000.0,
        "a small sample set keeps its outlier"
    );

    // Sorted: 99, 100, 100, 101, 102, 1000.
    check(
        few.samples == 6 &&
        few.median_us == 100.5,
        "median of six samples"
    );

    // Nine samples including one 900us outlier, so the MAD criterion
    // has enough to work with.
    const Stats many =
        aggregate(
            {
                100.0, 101.0, 99.0,
                100.0, 102.0, 98.0,
                100.0, 101.0, 1000.0
            }
        );

    check(
        many.outlier_check,
        "outlier rejection is attempted on nine samples"
    );

    check(
        many.outliers_removed == 1,
        "the distant sample is removed"
    );

    check(
        many.samples == 8 &&
        many.max_us == 102.0,
        "one outlier is dropped from nine samples"
    );

    check(
        many.median_us == 100.0,
        "median ignores the removed outlier"
    );

    // Bimodal samples: the median lands between the two modes and the
    // MAD collapses to the spread within them, so a free hand would
    // discard one mode entirely. Only one sample may go.
    const Stats bimodal =
        aggregate(
            {
                100.0, 100.0, 100.0,
                100.0, 101.0, 200.0,
                200.0, 200.0, 200.0
            }
        );

    check(
        bimodal.outliers_removed == 1,
        "no more than one sample is removed"
    );

    check(
        bimodal.samples == 8,
        "the rest of a bimodal sample set survives"
    );

    check(
        bimodal.median_us == 100.5,
        "median of the surviving bimodal samples"
    );

    // Every sample identical: the MAD is zero, so nothing can be
    // judged an outlier and nothing is removed.
    const Stats identical =
        aggregate({100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0});

    check(
        identical.outliers_removed == 0 &&
        identical.samples == 7,
        "identical samples are all kept"
    );

    check(
        identical.stdev_us == 0.0,
        "identical samples have no spread"
    );

    const Stats spread =
        aggregate({1.0, 2.0});

    check(
        spread.stdev_us > 0.70 &&
        spread.stdev_us < 0.71,
        "sample standard deviation"
    );
}

// ============================================================
// JSON
// ============================================================

void test_json()
{
    check_equal(
        json_escape("plain"),
        "plain",
        "escaping leaves an ordinary string alone"
    );

    check_equal(
        json_escape("a\"b"),
        "a\\\"b",
        "escaping quotes a double quote"
    );

    check_equal(
        json_escape("a\\b"),
        "a\\\\b",
        "escaping doubles a backslash"
    );

    check_equal(
        json_escape("a\nb"),
        "a\\nb",
        "escaping turns a newline into an escape"
    );

    string value;

    check(
        json_get_string(
            R"({"commit": "abc123"})",
            "commit",
            value
        )
        &&
        value == "abc123",
        "a string field is read"
    );

    check(
        json_get_string(
            R"({"a": "x", "commit": "y"})",
            "commit",
            value
        )
        &&
        value == "y",
        "the right field is read when others precede it"
    );

    check(
        !json_get_string(
            R"({"commit": 5})",
            "commit",
            value
        ),
        "a number is not read as a string"
    );

    check(
        !json_get_string(
            R"({"other": "x"})",
            "commit",
            value
        ),
        "a missing string field is reported missing"
    );

    double number = 0.0;

    check(
        json_get_number(
            R"({"median_us": 173991.00})",
            "median_us",
            number
        )
        &&
        number == 173991.0,
        "a number field is read"
    );

    // The keys of a baseline overlap: "runs" is a suffix of
    // "warmup_runs", and a prefix match would read the wrong field.
    check(
        !json_get_number(
            R"({"warmup_runs": 1})",
            "runs",
            number
        ),
        "a key that only appears as a suffix is not matched"
    );

    check(
        json_get_number(
            R"({"warmup_runs": 1})",
            "warmup_runs",
            number
        )
        &&
        number == 1.0,
        "the longer key is matched on its own"
    );

    check(
        json_get_number(
            R"({"change_percent": -13.91})",
            "change_percent",
            number
        )
        &&
        number == -13.91,
        "a negative number is read"
    );

    // Whatever json_escape produces has to survive being read back.
    const string awkward =
        "a\\b\"c\nd";

    const string document =
        "{\"cpu\": \"" +
        json_escape(awkward) +
        "\"}";

    check(
        json_get_string(
            document,
            "cpu",
            value
        )
        &&
        value == awkward,
        "an escaped string round trips"
    );
}

// ============================================================
// Baseline
// ============================================================

void test_baseline()
{
    const fs::path project_root =
        session_temp_directory() /
        "baseline-project";

    Baseline written;

    fill_environment_metadata(written);

    written.benchmark = BENCHMARK_NAME;
    written.commit = "41f6091";
    written.average_us = 203027.80;
    written.median_us = 202113.00;
    written.stdev_us = 1659.47;
    written.min_us = 201872.00;
    written.max_us = 205849.00;
    written.runs = 5;
    written.warmup_runs = 1;

    string error;

    check(
        write_baseline(
            project_root,
            written,
            error
        ),
        "a baseline is written"
    );

    check(
        fs::exists(
            project_root /
            ".perfguard" /
            "baseline.json"
        ),
        "the baseline lands in .perfguard"
    );

    Baseline read;

    check(
        read_baseline(
            project_root,
            read,
            error
        ),
        "the baseline is read back"
    );

    check(
        read.schema_version ==
        Baseline::CURRENT_SCHEMA_VERSION,
        "the schema version survives the round trip"
    );

    check_equal(
        read.benchmark,
        BENCHMARK_NAME,
        "the benchmark name survives the round trip"
    );

    check_equal(
        read.commit,
        "41f6091",
        "the commit survives the round trip"
    );

    check(
        read.average_us == 203027.80 &&
        read.median_us == 202113.00 &&
        read.min_us == 201872.00 &&
        read.max_us == 205849.00,
        "the measurements survive the round trip"
    );

    check(
        read.runs == 5 &&
        read.warmup_runs == 1,
        "the run counts survive the round trip"
    );

    // The CPU description contains a comma, so it would break a
    // hand-built document that forgot to escape.
    check_equal(
        read.cpu,
        written.cpu,
        "the cpu description survives the round trip"
    );

    check_equal(
        read.build_type,
        written.build_type,
        "the build type survives the round trip"
    );

    check_equal(
        read.cmake_generator,
        written.cmake_generator,
        "the generator survives the round trip"
    );

    string reason;

    check(
        baseline_comparable(read, reason),
        "a baseline this build wrote is comparable"
    );

    // A version 1 baseline has no metadata, so it is refused rather
    // than compared.
    const fs::path old_file =
        project_root /
        ".perfguard" /
        "baseline.json";

    {
        ofstream old_baseline(old_file);

        old_baseline
            << "{\n"
            << "    \"benchmark\": \"sort_1m\",\n"
            << "    \"commit\": \"80d7fc3\",\n"
            << "    \"average_us\": 180409.00\n"
            << "}\n";
    }

    Baseline old_read;

    check(
        read_baseline(
            project_root,
            old_read,
            error
        ),
        "a version 1 baseline is still readable"
    );

    check(
        old_read.schema_version == 1,
        "a baseline with no version reads as version 1"
    );

    check(
        old_read.average_us == 180409.0,
        "a version 1 average is read"
    );

    check(
        !baseline_comparable(old_read, reason),
        "a version 1 baseline is not comparable"
    );

    check(
        !reason.empty(),
        "the refusal says why"
    );

    // A different build configuration is refused too.
    Baseline release = written;

    release.build_type = "Release";

    check(
        !baseline_comparable(release, reason),
        "a baseline from another build type is not comparable"
    );

    // A baseline for another benchmark is refused.
    Baseline other_benchmark = written;

    other_benchmark.benchmark = "other";

    check(
        !baseline_comparable(other_benchmark, reason),
        "a baseline for another benchmark is not comparable"
    );

    // A missing file is reported rather than silently accepted.
    Baseline absent;

    check(
        !read_baseline(
            session_temp_directory() /
            "project-with-no-baseline",
            absent,
            error
        ),
        "a missing baseline is reported"
    );

    check(
        !error.empty(),
        "a missing baseline says why"
    );
}

// ============================================================
// run_command
//
// These exercise the real process plumbing, so they need cmd.exe.
// ============================================================

void test_run_command()
{
    const CommandResult success =
        run_command(
            "exit 3",
            false,
            5000
        );

    check(
        !success.timed_out,
        "a fast command does not time out"
    );

    check(
        success.exit_code == 3,
        "the child's exit code is reported"
    );

    const CommandResult echoed =
        run_command(
            "echo hello",
            false,
            5000
        );

    check(
        echoed.output.find("hello") != string::npos,
        "standard output is captured"
    );

    // The error stream must stay out of the captured output when it is
    // not asked for: parsed call sites would otherwise read a git error
    // message as if it were data. This line reaches the console
    // instead, which is where it went before.
    const CommandResult separate =
        run_command(
            "echo hidden 1>&2",
            false,
            5000
        );

    check(
        separate.output.find("hidden") == string::npos,
        "standard error is not merged unless requested"
    );

    const CommandResult merged =
        run_command(
            "echo shown 1>&2",
            true,
            5000
        );

    check(
        merged.output.find("shown") != string::npos,
        "standard error is merged when requested"
    );

    // A child that never finishes must be killed rather than hang the
    // caller. ping runs for about four seconds here.
    const CommandResult slow =
        run_command(
            "ping -n 5 127.0.0.1 >NUL",
            false,
            300
        );

    check(
        slow.timed_out,
        "a command past its deadline is reported as timed out"
    );

    check(
        slow.exit_code != 0,
        "a killed command does not report success"
    );
}

// ============================================================
// Shared constants
// ============================================================

void test_constants()
{
    check(
        BENCHMARK_SIZE > 0,
        "BENCHMARK_SIZE is positive"
    );

    check(
        BENCHMARK_RUNS > 0,
        "BENCHMARK_RUNS is positive"
    );

    check(
        DEFAULT_THRESHOLD_PERCENT > 0.0,
        "DEFAULT_THRESHOLD_PERCENT is positive"
    );
}

// ============================================================
// measure_interleaved
//
// The two sides are stubs that report a fixed average, so the
// structure can be checked without depending on how fast this
// machine sorts a million integers.
// ============================================================

void test_measure_interleaved()
{
    const fs::path directory =
        session_temp_directory() /
        "interleaved";

    error_code error;

    fs::create_directories(directory, error);

    const fs::path fast =
        directory / "fast.bat";

    const fs::path slow =
        directory / "slow.bat";

    write_text_file(
        fast,
        "@echo off\r\n"
        "echo Average: 100.00 us\r\n"
    );

    write_text_file(
        slow,
        "@echo off\r\n"
        "echo Average: 200.00 us\r\n"
    );

    Stats fast_stats;
    Stats slow_stats;

    string message;

    check(
        measure_interleaved(
            fast,
            slow,
            2,
            2,
            fast_stats,
            slow_stats,
            message
        ),
        "two executables are measured in alternating rounds"
    );

    check(
        fast_stats.samples == 4 &&
        slow_stats.samples == 4,
        "every round contributes to both sides"
    );

    // If a round leaked into the other side's samples, the maximum
    // here would be the slow side's figure.
    check(
        fast_stats.median_us == 100.0 &&
        fast_stats.max_us == 100.0,
        "the first side keeps its own samples"
    );

    check(
        slow_stats.median_us == 200.0 &&
        slow_stats.min_us == 200.0,
        "the second side keeps its own samples"
    );

    // A missing executable has to be reported rather than measured as
    // zero, which a comparison would read as an improvement.
    Stats ignored_a;
    Stats ignored_b;

    check(
        !measure_interleaved(
            directory / "absent.bat",
            fast,
            1,
            1,
            ignored_a,
            ignored_b,
            message
        ),
        "a missing executable is reported"
    );

    check(
        !message.empty(),
        "the missing executable says why"
    );

    check(
        !measure_interleaved(
            fast,
            slow,
            0,
            1,
            ignored_a,
            ignored_b,
            message
        ),
        "a round count of zero is refused"
    );
}

// ============================================================
// analyze_commit
// ============================================================

// The commit guaranteed to exist in the repository the tests are
// built from. Skips a merge, whose diff git does not print by
// default and which analyze_commit therefore reports as unreadable.
string readable_commit()
{
    return trim(
        run_command(
            "git rev-list --no-merges -1 HEAD",
            false,
            60000
        ).output
    );
}

void test_analyze_commit()
{
    // Refused before any git command runs, so no repository is needed.
    const EvidenceReport invalid =
        analyze_commit("zzz");

    check(
        !invalid.success,
        "a too-short commit hash is not analysed"
    );

    check(
        !invalid.error.empty(),
        "a refused commit says why"
    );

    check(
        !analyze_commit("").success,
        "an empty commit hash is not analysed"
    );

    // Well formed, but not in the repository: it has to fail rather
    // than report empty evidence about it.
    check(
        !analyze_commit(
            "0000000000000000000000000000000000000000"
        ).success,
        "a commit that does not exist is not analysed"
    );

    const string commit =
        readable_commit();

    if (!is_valid_commit_hash(commit)) {
        printf("SKIP: no readable commit to analyse\n");
        return;
    }

    const EvidenceReport report =
        analyze_commit(commit);

    check(
        report.success,
        "a real commit is analysed"
    );

    check_equal(
        report.commit,
        commit,
        "the full hash is reported"
    );

    check_equal(
        report.short_commit,
        commit.substr(0, 7),
        "the short hash is the first seven characters"
    );

    check(
        !report.message.empty(),
        "the commit message is read"
    );

    check(
        !report.author.empty(),
        "the author is read"
    );
}

// ============================================================
// CLI contract
//
// Drives the executable that ships, so the exit codes and the
// promise that --json writes its document on stdout are checked
// against the real thing rather than a copy of the logic.
// ============================================================

struct CliResult
{
    int exit_code = -1;

    string out;
    string err;
};

CliResult run_cli(
    const fs::path& executable,
    const string& arguments,
    const fs::path& working_directory = fs::path()
)
{
    const fs::path directory =
        session_temp_directory() /
        "cli";

    error_code error;

    fs::create_directories(directory, error);

    const fs::path out_path =
        directory / "stdout.txt";

    const fs::path err_path =
        directory / "stderr.txt";

    // The whole command is wrapped in a further pair of quotes: cmd.exe
    // strips the first and last quote of what follows /C when the
    // command begins with one and holds more than a single quoted
    // token, which would otherwise leave the line unbalanced.
    string command = "\"";

    if (!working_directory.empty()) {
        command +=
            "cd /d \"" +
            working_directory.string() +
            "\" && ";
    }

    command +=
        "\"" +
        executable.string() +
        "\" " +
        arguments +
        " > \"" +
        out_path.string() +
        "\" 2> \"" +
        err_path.string() +
        "\"";

    command += "\"";

    const CommandResult result =
        run_command(
            command,
            false,
            60000
        );

    CliResult cli;

    cli.exit_code = result.exit_code;
    cli.out = read_text_file(out_path);
    cli.err = read_text_file(err_path);

    return cli;
}

void test_cli_contract(
    const fs::path& perfguard
)
{
    if (!fs::exists(perfguard)) {
        printf(
            "SKIP: %s not found\n",
            perfguard.string().c_str()
        );

        return;
    }

    const CliResult version =
        run_cli(perfguard, "--version");

    check(
        version.exit_code == 0,
        "--version exits 0"
    );

    check(
        version.out.find(PERFGUARD_VERSION) != string::npos,
        "--version reports the version on stdout"
    );

    const CliResult help =
        run_cli(perfguard, "--help");

    check(
        help.exit_code == 0 &&
        help.out.find("Usage:") != string::npos,
        "--help prints the usage on stdout"
    );

    const CliResult unknown =
        run_cli(perfguard, "--not-a-command");

    check(
        unknown.exit_code == 2,
        "an unknown option exits 2"
    );

    check(
        unknown.err.find("Unknown option") != string::npos,
        "an unknown option is named on stderr"
    );

    // A command given too few arguments must not fall through to the
    // plain benchmark run, so each one is asked to say so.
    check(
        run_cli(perfguard, "--diagnose").exit_code == 2 &&
        run_cli(perfguard, "--evidence").exit_code == 2 &&
        run_cli(perfguard, "--bisect-test 123").exit_code == 2,
        "a command given too few arguments exits 2"
    );

    check(
        run_cli(perfguard, "--threshold nope --check").exit_code == 2,
        "a non-numeric threshold exits 2"
    );

    // Under --json the document is the only thing on stdout whatever
    // the outcome, and the narration moves out of the way.
    const CliResult json_error =
        run_cli(perfguard, "--evidence zzz --json");

    check(
        json_error.exit_code == 2,
        "a failing --json command still exits 2"
    );

    check(
        json_error.out.find("\"status\": \"error\"") != string::npos,
        "a failing --json command writes an error document on stdout"
    );

    check(
        json_error.out.find("Evidence analysis failed") == string::npos,
        "the human narration stays off stdout under --json"
    );

    check(
        json_error.err.find("Evidence analysis failed") != string::npos,
        "the human narration moves to stderr under --json"
    );

    check(
        json_error.out.rfind("{\n", 0) == 0 &&
        !json_error.out.empty() &&
        json_error.out.back() == '\n',
        "the document is the whole of stdout"
    );

    // A command line that cannot be parsed is still a command line
    // asked for a document, and the usage text is narration: it must
    // not be the thing stdout carries. --json is written last here, as
    // it is in the CI examples, so the parse must reach it after the
    // word it could not understand rather than stop there.
    const CliResult json_unknown =
        run_cli(perfguard, "--check --not-a-command --json");

    check(
        json_unknown.exit_code == 2 &&
        json_unknown.out.find("\"status\": \"error\"") != string::npos,
        "an unknown option writes an error document under --json"
    );

    check(
        json_unknown.out.find("Usage:") == string::npos &&
        json_unknown.err.find("Usage:") != string::npos,
        "the usage text moves to stderr under --json"
    );

    const CliResult json_bad_value =
        run_cli(perfguard, "--check --runs nope --json");

    check(
        json_bad_value.exit_code == 2 &&
        json_bad_value.out.find("\"command\": \"--check\"") != string::npos &&
        json_bad_value.out.find("\"status\": \"error\"") != string::npos &&
        json_bad_value.out.find("Usage:") == string::npos,
        "a bad option value still reaches --json written after it"
    );

    const CliResult json_short =
        run_cli(perfguard, "--diagnose --json");

    check(
        json_short.exit_code == 2 &&
        json_short.out.find("\"command\": \"--diagnose\"") != string::npos &&
        json_short.out.find("\"status\": \"error\"") != string::npos,
        "a command given too few arguments writes an error document under --json"
    );

    check(
        json_short.out.find("Usage:") == string::npos &&
        json_short.err.find("--diagnose needs a commit.") != string::npos,
        "its usage text and reason both move to stderr"
    );

    // The default command needs a project to read a baseline from,
    // and says so in a document rather than only in prose.
    const fs::path outside =
        session_temp_directory() /
        "no-project";

    error_code error;

    fs::create_directories(outside, error);

    const CliResult no_project =
        run_cli(perfguard, "--json", outside);

    check(
        no_project.exit_code == 2,
        "a run outside a project exits 2"
    );

    check(
        no_project.out.find("\"command\": \"benchmark\"") != string::npos &&
        no_project.out.find("\"status\": \"error\"") != string::npos,
        "a run outside a project writes an error document"
    );

    // A baseline names a commit, so it may only be recorded from a
    // tree that commit describes. This runs in a throwaway repository
    // so the refusal is read without touching this checkout's baseline.
    const fs::path scratch =
        session_temp_directory() /
        "dirty-tree";

    fs::create_directories(scratch, error);

    const CommandResult initialised =
        run_command(
            "cd /d \"" +
            scratch.string() +
            "\" && git init",
            true,
            60000
        );

    if (initialised.exit_code != 0) {
        printf("SKIP: could not create a scratch repository\n");
        return;
    }

    // The untracked file is what makes the tree dirty; a project root
    // is recognised by a .git directory beside a CMakeLists.txt.
    write_text_file(
        scratch / "CMakeLists.txt",
        "# scratch\n"
    );

    const CliResult dirty =
        run_cli(perfguard, "baseline --json", scratch);

    check(
        dirty.exit_code == 2,
        "baseline from a dirty tree exits 2"
    );

    check(
        dirty.out.find("\"command\": \"baseline\"") != string::npos &&
        dirty.out.find("\"status\": \"error\"") != string::npos,
        "baseline from a dirty tree writes an error document"
    );

    check(
        dirty.err.find("not clean") != string::npos,
        "baseline from a dirty tree says why"
    );

    // An array field is written as one line of quoted strings; read
    // it out of a real command so the shape checked is the one that
    // ships.
    const string commit =
        readable_commit();

    if (!is_valid_commit_hash(commit)) {
        printf("SKIP: no readable commit for --evidence\n");
        return;
    }

    const CliResult evidence =
        run_cli(
            perfguard,
            "--evidence " + commit + " --json"
        );

    check(
        evidence.exit_code == 0,
        "--evidence for a real commit exits 0"
    );

    check(
        evidence.out.find("\"changed_files\": [") != string::npos,
        "a string array field is written as an array"
    );

    check(
        evidence.out.find("\"performance_evidence\": [") != string::npos,
        "an empty-candidate array field is still written"
    );
}

// ============================================================
// The two spreads a document carries
//
// The noise figure is twice the larger relative standard deviation
// of the samples behind the two medians, so a document holding both
// spreads lets a reader work it out. Driven against a baseline
// written here whose own spread is far wider than a live measurement
// of this benchmark reaches, so the figure it forces is known in
// advance and a comparison that quietly used one side would read
// too low to match.
// ============================================================

void test_cli_json_spreads(
    const fs::path& perfguard
)
{
    if (!fs::exists(perfguard)) {
        printf("SKIP: %s not found\n", perfguard.string().c_str());
        return;
    }

    const fs::path scratch =
        session_temp_directory() /
        "spread-project";

    error_code error;

    fs::create_directories(scratch, error);

    write_text_file(
        scratch / "CMakeLists.txt",
        "# scratch\n"
    );

    const CommandResult initialised =
        run_command(
            "cd /d \"" +
            scratch.string() +
            "\" && git init",
            true,
            60000
        );

    if (initialised.exit_code != 0) {
        printf("SKIP: could not create a scratch repository\n");
        return;
    }

    // A relative standard deviation of a quarter, which the figure
    // doubles to 50%: wider than the samples behind a live median of
    // this benchmark, so the baseline is the side that decides it.
    Baseline baseline;

    fill_environment_metadata(baseline);

    baseline.benchmark = BENCHMARK_NAME;
    baseline.commit = "41f6091";
    baseline.average_us = 200000.0;
    baseline.median_us = 200000.0;
    baseline.stdev_us = 50000.0;
    baseline.min_us = 100000.0;
    baseline.max_us = 300000.0;
    baseline.runs = 5;
    baseline.warmup_runs = 1;

    string message;

    if (!write_baseline(scratch, baseline, message)) {
        printf("SKIP: could not write a scratch baseline\n");
        return;
    }

    const CliResult measured =
        run_cli(perfguard, "--runs 2 --json", scratch);

    check(
        measured.exit_code == 0 ||
        measured.exit_code == 1,
        "a scratch project measures its working tree"
    );

    check(
        measured.out.find("\"baseline_stats\"") != string::npos &&
        measured.out.find("\"current_stats\"") != string::npos,
        "a comparison document carries both spreads"
    );

    double noise = 0.0;

    check(
        json_get_number(
            measured.out,
            "noise_percent",
            noise
        )
        &&
        noise == 50.0,
        "the noise figure is drawn from both spreads"
    );

    // One sample leaves nothing to draw a spread from. The field is
    // then left out rather than written as a zero, which would read
    // as a perfectly steady measurement.
    Baseline single = baseline;

    single.runs = 1;

    if (!write_baseline(scratch, single, message)) {
        printf("SKIP: could not write a scratch baseline\n");
        return;
    }

    const CliResult unmeasurable =
        run_cli(perfguard, "--runs 1 --json", scratch);

    check(
        unmeasurable.out.find("\"baseline_stats\"") != string::npos &&
        unmeasurable.out.find("noise_percent") == string::npos,
        "no noise figure is written when no side has a spread"
    );
}

} // namespace

int main(
    int argc,
    char* argv[]
)
{
    test_trim();
    test_is_valid_commit_hash();
    test_parse_average();
    test_calculate_change();
    test_extract_first_bad_commit();
    test_analyze_added_line();
    test_aggregate();
    test_json();
    test_baseline();
    test_run_command();
    test_measure_interleaved();
    test_analyze_commit();
    test_constants();

    // The other executables are built beside this one, so the CLI is
    // driven from where the build put it rather than from a guess.
    if (argc > 0) {
        const fs::path directory =
            fs::absolute(argv[0]).parent_path();

        test_cli_contract(
            directory / "perfguard.exe"
        );

        test_cli_json_spreads(
            directory / "perfguard.exe"
        );
    }

    printf(
        "\n%d checks, %d failures\n",
        checks,
        failures
    );

    if (failures != 0) {
        return 1;
    }

    printf("All tests passed.\n");

    return 0;
}
