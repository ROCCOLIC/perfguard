#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <string>
#include <cstdio>
#include <thread>
int main() {
    const int N = 1'000'000;
    const int RUNS = 10;
    // =========================
    // 1. Get current Git commit
    // =========================
    std::string git_commit;
    FILE* pipe = _popen(
        "git rev-parse --short HEAD",
        "r"
    );
    if (pipe) {
        char buffer[128];
        if (fgets(
                buffer,
                sizeof(buffer),
                pipe)) {
            git_commit = buffer;
        }
        _pclose(pipe);
    }
    if (!git_commit.empty() &&
        git_commit.back() == '\n') {
        git_commit.pop_back();
    }
    std::cout
        << "PerfGuard Benchmark\n";
    std::cout
        << "===================\n";
    std::cout
        << "Git Commit: "
        << git_commit
        << "\n\n";
    // =========================
    // 2. Run benchmark
    // =========================
    std::vector<long long> results;
    for (int run = 1; run <= RUNS; ++run) {
        std::vector<int> data(N);
        for (int i = 0; i < N; ++i) {
            data[i] = N - i;
        }
        auto start =
            std::chrono::high_resolution_clock::now();
        std::sort(
            data.begin(),
            data.end()
        );
        // Artificial regression.
        // This is only for testing.
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10)
        );
        auto end =
            std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(end - start);
        results.push_back(
            duration.count()
        );
        std::cout
            << "Run "
            << run
            << ": "
            << duration.count()
            << " us\n";
    }
    // =========================
    // 3. Calculate statistics
    // =========================
    auto min_time =
        *std::min_element(
            results.begin(),
            results.end()
        );
    auto max_time =
        *std::max_element(
            results.begin(),
            results.end()
        );
    auto total =
        std::accumulate(
            results.begin(),
            results.end(),
            0LL
        );
    double average =
        static_cast<double>(total)
        / results.size();
    std::cout << "\n";
    std::cout
        << "Statistics\n";
    std::cout
        << "==========\n";
    std::cout
        << "Min:     "
        << min_time
        << " us\n";
    std::cout
        << "Max:     "
        << max_time
        << " us\n";
    std::cout
        << "Average: "
        << average
        << " us\n";
    // =========================
    // 4. Baseline
    // =========================
    std::filesystem::create_directories(
        ".perfguard"
    );
    const std::string baseline_path =
        ".perfguard/baseline.json";
    // =========================
    // 5. First run
    // =========================
    if (!std::filesystem::exists(
            baseline_path)) {
        std::ofstream file(
            baseline_path
        );
        if (!file) {
            std::cerr
                << "Failed to create baseline file.\n";
            return 1;
        }
        file << std::fixed
             << std::setprecision(2);
        file << "{\n";
        file
            << "    \"benchmark\": \"sort_1m\",\n";
        file
            << "    \"commit\": \""
            << git_commit
            << "\",\n";
        file
            << "    \"average_us\": "
            << average
            << "\n";
        file << "}\n";
        file.close();
        std::cout << "\n";
        std::cout
            << "Baseline created.\n";
        std::cout
            << "Commit: "
            << git_commit
            << "\n";
        std::cout
            << "Average: "
            << average
            << " us\n";
    }
    // =========================
    // 6. Compare with baseline
    // =========================
    else {
        std::ifstream file(
            baseline_path
        );
        if (!file) {
            std::cerr
                << "Failed to read baseline file.\n";
            return 1;
        }
        std::string line;
        double baseline = 0.0;
        std::string baseline_commit;
        while (std::getline(
            file,
            line)) {
            // Read baseline commit
            const std::string commit_key =
                "\"commit\":";
            auto commit_pos =
                line.find(commit_key);
            if (commit_pos != std::string::npos) {
                std::string value =
                    line.substr(
                        commit_pos
                        + commit_key.length()
                    );
                auto first_quote =
                    value.find('"');
                auto second_quote =
                    value.find(
                        '"',
                        first_quote + 1
                    );
                if (first_quote !=
                    std::string::npos &&
                    second_quote !=
                    std::string::npos) {

                    baseline_commit =
                        value.substr(
                            first_quote + 1,
                            second_quote
                            - first_quote
                            - 1
                        );
                }
            }
            // Read baseline average
            const std::string average_key =
                "\"average_us\":";
            auto average_pos =
                line.find(average_key);
            if (average_pos !=
                std::string::npos) {
                std::string value =
                    line.substr(
                        average_pos
                        + average_key.length()
                    );
                baseline =
                    std::stod(value);
            }
        }
        file.close();
        if (baseline <= 0.0) {
            std::cerr
                << "Invalid baseline.\n";
            return 1;
        }
        // =========================
        // 7. Calculate regression
        // =========================
        double change =
            ((average - baseline)
            / baseline)
            * 100.0;
        const double threshold = 10.0;
        std::cout << "\n";
        std::cout
            << "Performance Comparison\n";
        std::cout
            << "======================\n";
        std::cout
            << "Baseline Commit: "
            << baseline_commit
            << "\n";
        std::cout
            << "Current Commit:  "
            << git_commit
            << "\n";
        std::cout
            << "Baseline: "
            << baseline
            << " us\n";
        std::cout
            << "Current:  "
            << average
            << " us\n";
        std::cout
            << "Change:   "
            << change
            << "%\n";
        std::cout
            << "Threshold: "
            << threshold
            << "%\n";
        // =========================
        // 8. Final result
        // =========================
        if (change > threshold) {
            std::cout << "\n";
            std::cout
                << "WARNING: "
                << "Performance Regression!\n";
        }
        else {
            std::cout << "\n";
            std::cout
                << "Performance OK.\n";
        }
    }
    return 0;
}