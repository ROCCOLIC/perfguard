#pragma once

#include <string>

#include "perfguard_config.h"

struct BisectResult
{
    bool success = false;

    std::string baseline_commit;
    std::string first_bad_commit;

    std::string output;
    std::string error;
};

// Finds the first bad commit by driving `git bisect` in the project
// repository. Refuses to start while a bisect session is already in
// progress unless force is set, and puts HEAD back on the way out.
//
// threshold_percent is the one the caller detected a regression with:
// it decides which commits the bisect test calls bad, so passing a
// different one here than the caller used would bisect towards a
// different answer than the one being investigated.
BisectResult run_bisect(
    const std::string& executable_path,
    bool force = false,
    double threshold_percent =
        perfguard::DEFAULT_THRESHOLD_PERCENT
);

int run_bisect_test(
    const std::string& baseline_us,
    const std::string& threshold
);