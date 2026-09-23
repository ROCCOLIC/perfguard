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

// Finds the first bad commit by driving `git bisect` in the project
// repository. Refuses to start while a bisect session is already in
// progress unless force is set, and puts HEAD back on the way out.
BisectResult run_bisect(
    const std::string& executable_path,
    bool force = false
);

int run_bisect_test(
    const std::string& baseline_us,
    const std::string& threshold
);