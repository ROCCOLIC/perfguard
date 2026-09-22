
#include <algorithm>
#include <chrono>
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
#include <array>
#include "evidence.h"
#include "diagnosis.h"
#include "bisect.h"
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

string run_git_command(
    const string& command
)
{
    array<char, 4096> buffer{};

    string result;

    FILE* pipe =
        _popen(
            command.c_str(),
            "r"
        );

    if (
        pipe == nullptr
    ) {
        return {};
    }

    while (
        fgets(
            buffer.data(),
            static_cast<int>(buffer.size()),
            pipe
        ) != nullptr
    ) {
        result += buffer.data();
    }

    _pclose(
        pipe
    );

    return result;
}


// ============================================================
// Git commit
// ============================================================

string get_git_commit()
{
    string result =
        run_git_command(
            "git rev-parse HEAD"
        );

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

double run_benchmark()
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
// Artificial regression benchmark
// ============================================================

double run_artificial_regression_benchmark()
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
            chrono::milliseconds(10)
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
// Calculate performance change
// ============================================================

double calculate_change(
    double baseline,
    double current
)
{
    if (
        baseline <= 0.0
    ) {
        return 0.0;
    }

    return
        (
            (
                current -
                baseline
            )
            /
            baseline
        )
        * 100.0;
}


// ============================================================
// Baseline
// ============================================================

bool create_baseline(
    double average,
    const string& commit
)
{
    filesystem::path baseline_path =
        BASELINE_PATH;

    filesystem::path parent =
        baseline_path.parent_path();

    if (
        !parent.empty()
    ) {
        error_code error;

        filesystem::create_directories(
            parent,
            error
        );

        if (error) {
            cerr
                << "ERROR: Could not create "
                << parent.string()
                << "\n";

            return false;
        }
    }

    ofstream file(
        baseline_path
    );

    if (
        !file
    ) {
        cerr
            << "ERROR: Could not create baseline file.\n";

        return false;
    }

    file
        << "{\n"
        << "    \"benchmark\": \"sort_1m\",\n"
        << "    \"commit\": \""
        << commit
        << "\",\n"
        << "    \"average_us\": "
        << fixed
        << setprecision(2)
        << average
        << "\n"
        << "}\n";

    file.close();

    cout
        << "\nBaseline created successfully.\n";

    cout
        << "File: "
        << BASELINE_PATH
        << "\n";

    return true;
}


// ============================================================
// Load baseline average
// ============================================================

bool load_baseline_average(
    double& average
)
{
    if (
        !filesystem::exists(
            BASELINE_PATH
        )
    ) {
        return false;
    }

    ifstream file(
        BASELINE_PATH
    );

    if (
        !file
    ) {
        return false;
    }

    string content;

    {
        stringstream buffer;

        buffer
            << file.rdbuf();

        content =
            buffer.str();
    }

    static const regex pattern(
        R"("average_us"\s*:\s*([0-9.]+))"
    );

    smatch match;

    if (
        !regex_search(
            content,
            match,
            pattern
        )
    ) {
        return false;
    }

    try {

        average =
            stod(
                match[1].str()
            );

    }
    catch (...) {

        return false;
    }

    return average > 0.0;
}


// ============================================================
// Load baseline commit
// ============================================================

bool load_baseline_commit(
    string& commit
)
{
    if (
        !filesystem::exists(
            BASELINE_PATH
        )
    ) {
        return false;
    }

    ifstream file(
        BASELINE_PATH
    );

    if (
        !file
    ) {
        return false;
    }

    string content;

    {
        stringstream buffer;

        buffer
            << file.rdbuf();

        content =
            buffer.str();
    }

    static const regex pattern(
        R"PG("commit"\s*:\s*"([^"]+)")PG"
    );

    smatch match;

    if (
        !regex_search(
            content,
            match,
            pattern
        )
    ) {
        return false;
    }

    commit =
        match[1].str();

    return true;
}


// ============================================================
// Show commit evidence
// ============================================================

void show_commit_evidence(
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

        return;
    }

    cout << "\n";

    cout
        << "========================================\n";

    cout
        << "Commit Evidence\n";

    cout
        << "========================================\n";

    cout << "\n";

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

    cout << "\n";
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
    //
    // Full performance regression diagnosis.
    // ========================================================
    if (
        argc >= 2 &&
        string(argv[1]) == "--bisect"
    ) {
        const BisectResult result =
            run_bisect();

        if (!result.output.empty()) {
            cout << result.output;
        }

        if (!result.success) {
            cerr
                << "Bisect failed: "
                << result.error
                << '\n';

            return 1;
        }

        cout << '\n';
        cout
            << "========================================\n";

        cout
            << "First Bad Commit\n";

        cout
            << "========================================\n";

        cout
            << "Commit: "
            << result.first_bad_commit
            << '\n';

        return 0;
    }
    if (
        argc >= 3 &&
        string(argv[1]) ==
            "--diagnose"
    )
    {
        const string commit =
            argv[2];

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

            return 1;
        }

        DiagnosisResult diagnosis =
            diagnose_commit(
                commit
            );

        if (
            !diagnosis.success
        ) {
            cerr
                << "Commit diagnosis failed: "
                << diagnosis.error
                << "\n";

            return 1;
        }

        cout << "\n";

        cout
            << "========================================\n";

        cout
            << "Performance Regression Diagnosis\n";

        cout
            << "========================================\n";

        cout << "\n";

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

        cout << "\n";

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
        argc >= 3 &&
        string(argv[1]) ==
            "--evidence"
    )
    {
        show_commit_evidence(
            argv[2]
        );

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

        double baseline =
            0.0;

        if (
            !load_baseline_average(
                baseline
            )
        ) {
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
            << REGRESSION_THRESHOLD
            << "%\n";

        if (
            change >
            REGRESSION_THRESHOLD
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
    ) {
        cout
            << "Commit: "
            << git_commit
            << "\n";
    }
    else {
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
        ) {
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
    ) {
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
            << REGRESSION_THRESHOLD
            << "%\n";

        if (
            change >
            REGRESSION_THRESHOLD
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
        << REGRESSION_THRESHOLD
        << "%\n";

    cout << "\n";

    if (
        change >
        REGRESSION_THRESHOLD
    ) {
        cout
            << "WARNING: Performance Regression!\n";

        return 1;
    }

    cout
        << "Performance OK.\n";

    return 0;
}
