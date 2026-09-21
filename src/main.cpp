#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <string>
#include<thread>
int main() {
    const int N = 1'000'000;
    const int RUNS = 10;
    std::vector<long long> results;
    std::cout << "PerfGuard Benchmark\n";
    std::cout << "===================\n";
    // =========================
    // 1. Run benchmark
    // =========================
    for (int run = 1; run <= RUNS; ++run) {
        std::vector<int> data(N);
        for (int i = 0; i < N; ++i) {
            data[i] = N - i;
        }
        auto start =
            std::chrono::high_resolution_clock::now();
        std::sort(data.begin(), data.end());
        auto end =
            std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(end - start);
        results.push_back(duration.count());

        std::cout << "Run "
                  << run
                  << ": "
                  << duration.count()
                  << " us\n";
    }
    // =========================
    // 2. Calculate statistics
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
    std::cout << "Statistics\n";
    std::cout << "==========\n";
    std::cout << "Min:     "
              << min_time
              << " us\n";
    std::cout << "Max:     "
              << max_time
              << " us\n";
    std::cout << "Average: "
              << average
              << " us\n";
    // =========================
    // 3. Baseline
    // =========================
    std::filesystem::create_directories(
        ".perfguard"
    );
    const std::string baseline_path =
        ".perfguard/baseline.json";
    // =========================
    // 4. First run
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
        file << "    \"benchmark\": \"sort_1m\",\n";
        file << "    \"average_us\": "
             << average
             << "\n";
        file << "}\n";
        file.close();
        std::cout << "\n";
        std::cout
            << "Baseline created.\n";
        std::cout
            << "Average: "
            << average
            << " us\n";
    }
    // =========================
    // 5. Compare with baseline
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
        while (std::getline(
            file,
            line)) {
            const std::string key =
                "\"average_us\":";
            auto pos =
                line.find(key);
            if (pos != std::string::npos) {
                std::string value =
                    line.substr(
                        pos + key.length()
                    );
                baseline =
                    std::stod(value);
                break;
            }
        }
        file.close();
        if (baseline <= 0.0) {
            std::cerr
                << "Invalid baseline.\n";
            return 1;
        }
        // =========================
        // 6. Calculate regression
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
        // 7. Final result
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