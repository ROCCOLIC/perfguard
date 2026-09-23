#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <thread>

#include "perfguard_config.h"

using namespace std;
using namespace perfguard;

double run_benchmark()
{
    double total_us = 0.0;

    for (int run = 0; run < BENCHMARK_RUNS; ++run)
    {
        vector<int> data;
        data.reserve(BENCHMARK_SIZE);

        mt19937 rng(
            123456 + run
        );

        uniform_int_distribution<int> distribution(
            0,
            BENCHMARK_SIZE
        );

        for (int i = 0; i < BENCHMARK_SIZE; ++i)
        {
            data.push_back(
                distribution(rng)
            );
        }

        auto start =
            chrono::high_resolution_clock::now();

        sort(
            data.begin(),
            data.end()
        );
        // The injected regression. It sleeps for a shared constant so
        // that the fixture and --simulate-regression inject the same
        // slowdown, and so that the margin over the threshold is not
        // quietly tuned down in one place and not the other.
        this_thread::sleep_for(
            chrono::milliseconds(FIXTURE_SLEEP_MS)
        );
        auto end =
            chrono::high_resolution_clock::now();

        chrono::duration<double, micro> elapsed =
            end - start;

        total_us += elapsed.count();
    }

    return total_us / BENCHMARK_RUNS;
}

int main()
{
    double average =
        run_benchmark();

    cout
        << "Benchmark: sort_1m\n";

    cout
        << "Runs: "
        << BENCHMARK_RUNS
        << "\n";

    cout
        << "Average: "
        << average
        << " us\n";

    return 0;
}