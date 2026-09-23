#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace perfguard {

// ============================================================
// Build artifacts
//
// A commit built in its own worktree and build directory. The
// directories are named after the commit and live under this
// process's session directory, so two runs never share a tree.
// ============================================================

struct BuildArtifacts
{
    std::filesystem::path project_root;
    std::filesystem::path worktree;
    std::filesystem::path build_dir;
    std::filesystem::path executable;

    // Configure and build output, kept so a failure can be shown
    // rather than summarized.
    std::string build_log;
};


// ============================================================
// Statistics
//
// The median is the figure to compare: unlike the mean it is not
// dragged around by a single slow run, which is the usual shape of
// the noise here.
// ============================================================

struct Stats
{
    double min_us = 0.0;
    double median_us = 0.0;
    double mean_us = 0.0;
    double max_us = 0.0;
    double stdev_us = 0.0;

    // Samples the figures above are based on, after any rejection.
    int samples = 0;

    // Samples dropped as outliers.
    int outliers_removed = 0;

    // Whether the outlier criterion was applied at all. It needs
    // enough samples to mean anything, and is skipped rather than
    // faked when there are too few.
    bool outlier_check = false;
};


// Builds commit into a worktree of its own. On failure error explains
// what went wrong and artifacts.build_log holds the compiler output.
bool prepare_build(
    const std::string& commit,
    BuildArtifacts& artifacts,
    std::string& error
);


// Removes the worktree and build directory. Safe to call on a
// partially prepared BuildArtifacts.
void release_build(
    BuildArtifacts& artifacts
);


Stats aggregate(
    const std::vector<double>& samples
);


// Runs the benchmark warmup times to settle the machine, then calls
// times and aggregates what it reports.
bool measure(
    const std::filesystem::path& executable,
    int calls,
    int warmup,
    Stats& stats,
    std::string& error
);


// Measures both executables in alternating rounds.
//
// Two commits cannot share a build directory, so each side carries its
// own; alternating between them spreads any drift in the machine or in
// build layout evenly across both, which a run of A followed by a run
// of B would not.
bool measure_interleaved(
    const std::filesystem::path& executable_a,
    const std::filesystem::path& executable_b,
    int rounds,
    int calls,
    Stats& stats_a,
    Stats& stats_b,
    std::string& error
);

}  // namespace perfguard
