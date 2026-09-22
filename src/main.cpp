#include <algorithm>
#include <array>
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

#include "evidence.h"
#include "diagnosis.h"
#include "bisect.h"

using namespace std;

namespace fs = std::filesystem;


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

const string BENCHMARK_EXECUTABLE =
    "build/Debug/sort_1m.exe";

string run_command(
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


bool run_benchmark(
    double& average
)
{
    fs::path executable =
        fs::absolute(
            BENCHMARK_EXECUTABLE
        );

    if (
        !fs::exists(
            executable
        )
    ) {
        cerr
            << "ERROR: Benchmark executable not found:\n"
            << "  "
            << executable.string()
            << "\n";

        cerr
            << "\nBuild it with:\n"
            << "  cmake --build build --config Debug\n";

        return false;
    }

    string output =
        run_command(
            "\"" +
            executable.string() +
            "\""
        );

    if (
        output.empty()
    ) {
        cerr
            << "ERROR: Benchmark produced no output.\n";

        return false;
    }

    static const regex pattern(
        R"(Average:\s*([0-9.]+)\s*us)"
    );

    smatch match;

    if (
        !regex_search(
            output,
            match,
            pattern
        )
    ) {
        cerr
            << "ERROR: Could not parse benchmark output.\n";

        cerr
            << "\nBenchmark output:\n"
            << output
            << "\n";

        return false;
    }

    try {
        average =
            stod(
                match[1].str()
            );
    }
    catch (...) {
        cerr
            << "ERROR: Invalid benchmark average.\n";

        return false;
    }

    cout
        << output;

    return average > 0.0;
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
    fs::path baseline_path =
        BASELINE_PATH;

    fs::path parent =
        baseline_path.parent_path();

    if (
        !parent.empty()
    ) {
        error_code error;

        fs::create_directories(
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
        !fs::exists(
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
        R"PG("average_us"\s*:\s*([0-9.]+))PG"
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
        !fs::exists(
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
    // --analyze
    //
    // Full automatic performance analysis:
    //
    //   1. Run current benchmark
    //   2. Detect regression
    //   3. Automatically bisect Git history
    //   4. Diagnose first bad commit
    //   5. Generate Markdown report
    // ========================================================

    if (
        argc >= 2 &&
        string(argv[1]) ==
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

        double baseline =
            0.0;

        string baseline_commit;

        if (
            !load_baseline_average(
                baseline
            )
        ) {
            cerr
                << "ERROR: Could not read baseline.\n";

            return 1;
        }

        load_baseline_commit(
            baseline_commit
        );

        double current =
            0.0;

        if (
            !run_benchmark(
                current
            )
        ) {
            return 1;
        }

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
            << REGRESSION_THRESHOLD
            << "%\n";


        // ----------------------------------------------------
        // No regression
        // ----------------------------------------------------

        if (
            change <=
            REGRESSION_THRESHOLD
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

        fs::path executable_path =
            fs::absolute(
                argv[0]
            );

        BisectResult bisect_result =
            run_bisect(
                executable_path.string()
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

            return 1;
        }

        const string first_bad_commit =
            bisect_result.first_bad_commit;

        if (
            first_bad_commit.empty()
        ) {
            cerr
                << "\nBisect did not return "
                   "a first bad commit.\n";

            return 1;
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

            return 1;
        }

        DiagnosisResult diagnosis =
            diagnose_commit(
                first_bad_commit
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

            return 1;
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
            << REGRESSION_THRESHOLD
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
        argc >= 2 &&
        string(argv[1]) ==
            "--report"
    )
    {
        double baseline =
            0.0;

        string baseline_commit;

        if (
            !load_baseline_average(
                baseline
            )
        ) {
            cerr
                << "ERROR: Could not read baseline average.\n";

            return 1;
        }

        load_baseline_commit(
            baseline_commit
        );

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
            << "\nRunning benchmark...\n";

        double current =
            0.0;

        if (
            !run_benchmark(
                current
            )
        ) {
            return 1;
        }

        double change =
            calculate_change(
                baseline,
                current
            );

        bool regression =
            change >
            REGRESSION_THRESHOLD;

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

            return 1;
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
            << REGRESSION_THRESHOLD
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
        argc >= 4 &&
        string(argv[1]) ==
            "--bisect-test"
    )
    {
        return run_bisect_test(
            argv[2],
            argv[3]
        );
    }


    // ========================================================
    // --bisect
    //
    // Automatically find the first commit that introduced
    // a performance regression.
    // ========================================================

    if (
        argc >= 2 &&
        string(argv[1]) ==
            "--bisect"
    )
    {
        fs::path executable_path =
            fs::absolute(
                argv[0]
            );

        const BisectResult result =
            run_bisect(
                executable_path.string()
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

            return 1;
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

        cout
            << "\n";

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

    cout
        << "\n";


    // --------------------------------------------------------
    // REAL benchmark
    // --------------------------------------------------------

    cout
        << "Running benchmark...\n";

    double average =
    0.0;

    if (
        !run_benchmark(
            average
        )
    ) {
        return 1;
    }

    cout
        << "\n";


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
        !fs::exists(
            BASELINE_PATH
        )
    )
    {
        cout
            << "\n";

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

    cout
        << "\n";

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

    cout
        << "\n";

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