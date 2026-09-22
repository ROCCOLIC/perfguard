#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

using namespace std;

const int BENCHMARK_SIZE = 1'000'000;
const int BENCHMARK_RUNS = 10;

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