// Dependency-free tests for the pure helpers in src/core.cpp.
//
// No third-party framework: the project builds with nothing but
// CMake and MSVC, and adding a test dependency to get assert
// macros is not worth it.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "core.h"
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

} // namespace

int main()
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
    test_constants();

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
