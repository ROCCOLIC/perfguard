#pragma once

#include <string>

struct BisectResult
{
    bool success = false;

    std::string baseline_commit;
    std::string first_bad_commit;

    std::string output;
    std::string error;
};

BisectResult run_bisect(
    const std::string& executable_path
);

int run_bisect_test(
    const std::string& baseline_us,
    const std::string& threshold
);