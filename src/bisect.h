#pragma once

#include <string>

struct BisectResult {
    bool success = false;

    std::string baseline_commit;
    std::string first_bad_commit;

    std::string output;
    std::string error;
};

BisectResult run_bisect();