#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std;

// ============================================================
// Configuration
// ============================================================

const int BENCHMARK_SIZE = 1'000'000;
const int BENCHMARK_RUNS = 10;

const double REGRESSION_THRESHOLD = 10.0;

const string BASELINE_PATH =
    ".perfguard/baseline.json";


// ============================================================
// Utility
// ============================================================

string trim(string value)
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


string run_command(const string& command)
{
    string result;

    FILE* pipe =
        _popen(
            command.c_str(),
            "r"
        );

    if (!pipe)
    {
        return "";
    }

    char buffer[4096];

    while (
        fgets(
            buffer,
            sizeof(buffer),
            pipe
        )
    )
    {
        result += buffer;
    }

    _pclose(pipe);

    return result;
}


// ============================================================
// Git
// ============================================================

string get_git_commit()
{
    string commit =
        run_command(
            "git rev-parse --short HEAD"
        );

    return trim(commit);
}


bool is_hex_commit(
    const string& commit
)
{
    if (commit.empty())
    {
        return false;
    }

    for (char c : commit)
    {
        if (
            !(
                (c >= '0' && c <= '9') ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F')
            )
        )
        {
            return false;
        }
    }

    return true;
}


// ============================================================
// Baseline
// ============================================================

bool load_baseline_average(
    double& average
)
{
    ifstream file(
        BASELINE_PATH
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

    regex pattern(
        R"("average_us"\s*:\s*([0-9]+(?:\.[0-9]+)?))"
    );

    smatch match;

    if (
        regex_search(
            content,
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


bool load_baseline_commit(
    string& commit
)
{
    ifstream file(
        BASELINE_PATH
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

    regex pattern(
        R"rx("commit"\s*:\s*"([^"]+)")rx"
    );

    smatch match;

    if (
        regex_search(
            content,
            match,
            pattern
        )
    )
    {
        commit =
            match[1].str();

        return true;
    }

    return false;
}


bool create_baseline(
    double average,
    const string& commit
)
{
    filesystem::create_directories(
        ".perfguard"
    );

    ofstream file(
        BASELINE_PATH
    );

    if (!file)
    {
        cerr
            << "ERROR: Could not create baseline file.\n";

        return false;
    }

    file << "{\n";

    file
        << "    \"benchmark\": \"sort_1m\",\n";

    file
        << "    \"commit\": \""
        << commit
        << "\",\n";

    file
        << fixed
        << setprecision(2);

    file
        << "    \"average_us\": "
        << average
        << "\n";

    file << "}\n";

    cout << "\n";

    cout
        << "Baseline created.\n";

    cout
        << "Commit: "
        << commit
        << "\n";

    cout
        << "Average: "
        << fixed
        << setprecision(2)
        << average
        << " us\n";

    return true;
}


// ============================================================
// Benchmark
// ============================================================
//
// IMPORTANT:
//
// This is the REAL benchmark.
//
// No artificial sleep is added here.
//
// That means:
//
// baseline
// check
// normal benchmark
//
// all measure the actual program performance.
//
// ============================================================

double run_benchmark()
{
    const int N =
        BENCHMARK_SIZE;

    const int RUNS =
        BENCHMARK_RUNS;

    vector<long long> times;

    times.reserve(
        RUNS
    );

    for (
        int run = 0;
        run < RUNS;
        ++run
    )
    {
        vector<int> data(
            N
        );

        for (
            int i = 0;
            i < N;
            ++i
        )
        {
            data[i] =
                N - i;
        }

        auto start =
            chrono::high_resolution_clock::now();

        sort(
            data.begin(),
            data.end()
        );

        auto end =
            chrono::high_resolution_clock::now();

        auto elapsed =
            chrono::duration_cast<
                chrono::microseconds
            >(
                end - start
            ).count();

        times.push_back(
            static_cast<long long>(
                elapsed
            )
        );
    }

    long long total = 0;

    for (
        long long value :
        times
    )
    {
        total += value;
    }

    double average =
        static_cast<double>(
            total
        ) /
        static_cast<double>(
            times.size()
        );

    return average;
}


// ============================================================
// Artificial Regression Test
// ============================================================
//
// This function is NOT used by:
//   - baseline
//   - normal benchmark
//   - --check
//   - --diagnose
//
// It only exists so we can manually test whether PerfGuard
// can detect an artificially introduced slowdown.
//
// ============================================================

double run_artificial_regression_benchmark()
{
    const int N =
        BENCHMARK_SIZE;

    const int RUNS =
        BENCHMARK_RUNS;

    vector<long long> times;

    times.reserve(
        RUNS
    );

    for (
        int run = 0;
        run < RUNS;
        ++run
    )
    {
        vector<int> data(
            N
        );

        for (
            int i = 0;
            i < N;
            ++i
        )
        {
            data[i] =
                N - i;
        }

        auto start =
            chrono::high_resolution_clock::now();

        sort(
            data.begin(),
            data.end()
        );

        // ----------------------------------------------------
        // Artificial regression.
        //
        // ONLY for testing PerfGuard.
        // ----------------------------------------------------

        this_thread::sleep_for(
            chrono::milliseconds(10)
        );

        auto end =
            chrono::high_resolution_clock::now();

        auto elapsed =
            chrono::duration_cast<
                chrono::microseconds
            >(
                end - start
            ).count();

        times.push_back(
            static_cast<long long>(
                elapsed
            )
        );
    }

    long long total = 0;

    for (
        long long value :
        times
    )
    {
        total += value;
    }

    return
        static_cast<double>(
            total
        ) /
        static_cast<double>(
            times.size()
        );
}


// ============================================================
// Diagnosis Benchmark
// ============================================================
//
// Diagnosis must measure the REAL benchmark.
//
// No artificial sleep.
//
// ============================================================

double run_diagnosis_benchmark()
{
    return run_benchmark();
}


// ============================================================
// Regression Calculation
// ============================================================

double calculate_change(
    double baseline,
    double current
)
{
    if (baseline <= 0.0)
    {
        return 0.0;
    }

    return
        (
            (current - baseline)
            / baseline
        ) * 100.0;
}


// ============================================================
// Evidence Engine
// ============================================================

void show_commit_evidence(
    const string& commit
)
{
    cout
        << "========================================\n";

    cout
        << "Performance Regression Evidence\n";

    cout
        << "========================================\n";

    // --------------------------------------------------------
    // Validate commit
    // --------------------------------------------------------

    if (
        !is_hex_commit(commit)
    )
    {
        cout
            << "Invalid commit hash.\n";

        return;
    }

    // --------------------------------------------------------
    // Commit information
    // --------------------------------------------------------

    string commit_full =
        run_command(
            "git show -s --format=%H "
            + commit
        );

    string message =
        run_command(
            "git show -s --format=%s "
            + commit
        );

    string author =
        run_command(
            "git show -s --format=%an "
            + commit
        );

    commit_full =
        trim(commit_full);

    message =
        trim(message);

    author =
        trim(author);

    cout
        << "Commit: "
        << commit_full
        << "\n";

    cout
        << "Message: "
        << message
        << "\n";

    cout
        << "Author: "
        << author
        << "\n\n";


    // --------------------------------------------------------
    // Changed files
    // --------------------------------------------------------

    cout
        << "Changed files:\n";

    string files =
        run_command(
            "git diff-tree "
            "--no-commit-id "
            "--name-status "
            "-r "
            + commit
        );

    if (files.empty())
    {
        cout
            << "  No changed files found.\n";
    }
    else
    {
        istringstream file_stream(
            files
        );

        string line;

        while (
            getline(
                file_stream,
                line
            )
        )
        {
            line =
                trim(line);

            if (!line.empty())
            {
                cout
                    << "  "
                    << line
                    << "\n";
            }
        }
    }

    cout << "\n";


    // --------------------------------------------------------
    // Diff statistics
    // --------------------------------------------------------

    string stat =
        run_command(
            "git show "
            "--format= "
            "--numstat "
            + commit
        );

    int lines_added = 0;
    int lines_removed = 0;

    istringstream stat_stream(
        stat
    );

    string stat_line;

    while (
        getline(
            stat_stream,
            stat_line
        )
    )
    {
        istringstream row(
            stat_line
        );

        int added = 0;
        int removed = 0;

        if (
            row
                >> added
                >> removed
        )
        {
            lines_added +=
                added;

            lines_removed +=
                removed;
        }
    }

    cout
        << "Change summary:\n";

    cout
        << "  Lines added:   "
        << lines_added
        << "\n";

    cout
        << "  Lines removed: "
        << lines_removed
        << "\n\n";


    // --------------------------------------------------------
    // Diff content
    // --------------------------------------------------------

    string diff =
        run_command(
            "git show "
            "--format= "
            "--no-ext-diff "
            + commit
            + " -- src"
        );


    // --------------------------------------------------------
    // Performance evidence
    // --------------------------------------------------------

    cout
        << "Potential performance evidence:\n";

    istringstream diff_stream(
        diff
    );

    string diff_line;

    bool evidence_found =
        false;

    while (
        getline(
            diff_stream,
            diff_line
        )
    )
    {
        if (
            diff_line.empty() ||
            diff_line[0] != '+'
        )
        {
            continue;
        }

        if (
            diff_line.rfind(
                "+++",
                0
            ) == 0
        )
        {
            continue;
        }

        string lower =
            diff_line;

        transform(
            lower.begin(),
            lower.end(),
            lower.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(
                    tolower(c)
                );
            }
        );

        bool performance_related =
            lower.find("sleep")
                != string::npos ||

            lower.find("sort")
                != string::npos ||

            lower.find("loop")
                != string::npos ||

            lower.find("thread")
                != string::npos ||

            lower.find("mutex")
                != string::npos ||

            lower.find("lock")
                != string::npos ||

            lower.find("memory")
                != string::npos ||

            lower.find("allocation")
                != string::npos ||

            lower.find("vector")
                != string::npos ||

            lower.find("copy")
                != string::npos ||

            lower.find("chrono")
                != string::npos;

        if (
            performance_related
        )
        {
            cout
                << "  "
                << diff_line
                << "\n";

            evidence_found =
                true;
        }
    }

    if (
        !evidence_found
    )
    {
        cout
            << "  No obvious performance-related "
               "changes detected.\n";
    }

    cout << "\n";


    // --------------------------------------------------------
    // Benchmark impact
    // --------------------------------------------------------

    double baseline = 0.0;

    if (
        !load_baseline_average(
            baseline
        )
    )
    {
        cout
            << "Benchmark impact:\n";

        cout
            << "  Baseline unavailable.\n\n";

        cout
            << "Diagnosis:\n";

        cout
            << "  Cannot compare performance "
               "without a baseline.\n";

        return;
    }

    cout
        << "Benchmark impact:\n";

    cout
        << "  Baseline: "
        << fixed
        << setprecision(2)
        << baseline
        << " us\n";

    cout
        << "  Running current benchmark...\n";

    double current =
        run_diagnosis_benchmark();

    double change =
        calculate_change(
            baseline,
            current
        );

    cout
        << "  Current:  "
        << fixed
        << setprecision(2)
        << current
        << " us\n";

    cout
        << "  Change:   ";

    if (change >= 0)
    {
        cout << "+";
    }

    cout
        << fixed
        << setprecision(2)
        << change
        << "%\n\n";


    // --------------------------------------------------------
    // Diagnosis
    // --------------------------------------------------------

    cout
        << "Diagnosis:\n";

    if (
        change >
        REGRESSION_THRESHOLD
    )
    {
        cout
            << "  Performance regression detected.\n";

        if (
            evidence_found
        )
        {
            cout
                << "  The commit contains changes "
                   "that may explain the regression.\n";
        }
        else
        {
            cout
                << "  No obvious performance-related "
                   "change was found in the commit.\n";
        }
    }
    else
    {
        cout
            << "  No significant performance "
               "regression detected.\n";
    }
}


// ============================================================
// Print Benchmark Result
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
// Main
// ============================================================

int main(
    int argc,
    char* argv[]
)
{
    // ========================================================
    // --diagnose <commit>
    // ========================================================

    if (
        argc >= 3 &&
        string(argv[1]) ==
            "--diagnose"
    )
    {
        show_commit_evidence(
            argv[2]
        );

        return 0;
    }


    // ========================================================
    // --simulate-regression
    // ========================================================
    //
    // This command intentionally creates a slowdown.
    //
    // It does NOT modify the baseline.
    //
    // ========================================================

    if (
        argc >= 2 &&
        string(argv[1]) ==
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

        double average =
            run_artificial_regression_benchmark();

        cout << "\n";

        print_benchmark_result(
            average
        );

        double baseline = 0.0;

        if (
            !load_baseline_average(
                baseline
            )
        )
        {
            cout
                << "\nNo baseline found.\n";

            return 1;
        }

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

        if (change >= 0)
        {
            cout << "+";
        }

        cout
            << fixed
            << setprecision(2)
            << change
            << "%\n";

        cout
            << "Threshold: "
            << REGRESSION_THRESHOLD
            << "%\n";

        if (
            change >
            REGRESSION_THRESHOLD
        )
        {
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
    // ========================================================

    bool baseline_mode =
        argc >= 2 &&
        string(argv[1]) ==
            "baseline";


    // ========================================================
    // --check
    // ========================================================

    bool check_mode =
        argc >= 2 &&
        string(argv[1]) ==
            "--check";


    // ========================================================
    // Normal benchmark
    // ========================================================

    cout
        << "================================\n";

    cout
        << "PerfGuard Performance Benchmark\n";

    cout
        << "================================\n";


    // --------------------------------------------------------
    // Git commit
    // --------------------------------------------------------

    string git_commit =
        get_git_commit();

    if (
        !git_commit.empty()
    )
    {
        cout
            << "Commit: "
            << git_commit
            << "\n";
    }
    else
    {
        cout
            << "Commit: unknown\n";
    }

    cout << "\n";


    // --------------------------------------------------------
    // REAL benchmark
    // --------------------------------------------------------

    cout
        << "Running benchmark...\n";

    double average =
        run_benchmark();

    cout << "\n";

    print_benchmark_result(
        average
    );


    // ========================================================
    // baseline mode
    // ========================================================

    if (
        baseline_mode
    )
    {
        if (
            !create_baseline(
                average,
                git_commit
            )
        )
        {
            return 1;
        }

        return 0;
    }


    // ========================================================
    // Baseline existence
    // ========================================================

    if (
        !filesystem::exists(
            BASELINE_PATH
        )
    )
    {
        cout << "\n";

        cout
            << "No baseline found.\n";

        cout
            << "Run:\n";

        cout
            << "  perfguard baseline\n";

        return 1;
    }


    // ========================================================
    // Load baseline
    // ========================================================

    double baseline_average =
        0.0;

    string baseline_commit;

    if (
        !load_baseline_average(
            baseline_average
        )
    )
    {
        cerr
            << "ERROR: Could not read "
               "baseline average.\n";

        return 1;
    }

    load_baseline_commit(
        baseline_commit
    );


    // ========================================================
    // Regression calculation
    // ========================================================

    double change =
        calculate_change(
            baseline_average,
            average
        );


    // ========================================================
    // --check
    // ========================================================

    if (
        check_mode
    )
    {
        cout
            << "\n";

        cout
            << "PerfGuard Check\n";

        cout
            << "===============\n";

        cout
            << "Baseline Commit: "
            << baseline_commit
            << "\n";

        cout
            << "Current Commit:  "
            << git_commit
            << "\n";

        cout
            << "Baseline: "
            << fixed
            << setprecision(2)
            << baseline_average
            << " us\n";

        cout
            << "Current:  "
            << fixed
            << setprecision(2)
            << average
            << " us\n";

        cout
            << "Change:   ";

        if (change >= 0)
        {
            cout << "+";
        }

        cout
            << fixed
            << setprecision(2)
            << change
            << "%\n";

        cout
            << "Threshold: "
            << REGRESSION_THRESHOLD
            << "%\n";

        if (
            change >
            REGRESSION_THRESHOLD
        )
        {
            cout
                << "Result: REGRESSION\n";

            return 1;
        }

        cout
            << "Result: OK\n";

        return 0;
    }


    // ========================================================
    // Normal result
    // ========================================================

    cout << "\n";

    cout
        << "Baseline Commit: "
        << baseline_commit
        << "\n";

    cout
        << "Current Commit:  "
        << git_commit
        << "\n";

    cout
        << "Baseline Average: "
        << fixed
        << setprecision(2)
        << baseline_average
        << " us\n";

    cout
        << "Current Average:  "
        << fixed
        << setprecision(2)
        << average
        << " us\n";

    cout
        << "Performance Change: ";

    if (change >= 0)
    {
        cout << "+";
    }

    cout
        << fixed
        << setprecision(2)
        << change
        << "%\n";

    cout
        << "Threshold: "
        << REGRESSION_THRESHOLD
        << "%\n";

    cout << "\n";


    if (
        change >
        REGRESSION_THRESHOLD
    )
    {
        cout
            << "WARNING: Performance Regression!\n";

        return 1;
    }

    cout
        << "Performance OK.\n";

    return 0;
}