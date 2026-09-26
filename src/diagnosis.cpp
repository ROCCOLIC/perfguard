#include "diagnosis.h"

#include <cstdio>
#include <filesystem>
#include <regex>
#include <string>

#include "core.h"
#include "measurement.h"
#include "perfguard_config.h"

using namespace perfguard;

namespace fs = std::filesystem;

// ============================================================
// Diagnose commit
// ============================================================

DiagnosisResult diagnose_commit(
    const std::string& commit,
    int calls,
    int warmup,
    double threshold_percent
)
{
    DiagnosisResult result;

    result.commit =
        commit;

    result.threshold_percent =
        threshold_percent;

    // ========================================================
    // Step 1
    // Validate commit
    // ========================================================

    if (
        !is_valid_commit_hash(
            commit
        )
    ) {

        result.error =
            "Invalid commit hash.";

        return result;
    }

    // ========================================================
    // Step 2
    // Find project root
    //
    // Done before the build so that a missing baseline is
    // reported in a second rather than after a full compile.
    // ========================================================

    const fs::path project_root =
        find_project_root();

    if (
        project_root.empty()
    ) {

        result.error =
            "Unable to find PerfGuard project root. "
            "Make sure the command is executed inside "
            "the PerfGuard project directory.";

        return result;
    }

    // ========================================================
    // Step 3
    // Read baseline
    // ========================================================

    Baseline stored;

    if (
        !read_baseline(
            project_root,
            stored,
            result.error
        )
    ) {
        return result;
    }

    std::string reason;

    if (
        !baseline_comparable(
            stored,
            reason
        )
    ) {
        result.error =
            reason +
            "\n  Run:\n"
            "    perfguard baseline";

        return result;
    }

    result.baseline_us =
        stored.median_us;

    // Carried in the shape a fresh measurement is described in, so the
    // caller can draw the noise of the comparison from both medians
    // without reading the baseline a second time.
    result.baseline_stats.samples =
        stored.runs;

    result.baseline_stats.min_us =
        stored.min_us;

    result.baseline_stats.max_us =
        stored.max_us;

    result.baseline_stats.median_us =
        stored.median_us;

    result.baseline_stats.mean_us =
        stored.average_us;

    result.baseline_stats.stdev_us =
        stored.stdev_us;

    // ========================================================
    // Step 4
    // Build the commit
    //
    // Shared with --check, so the worktree, the build and the
    // executable lookup exist in one place. On failure the
    // compiler output is handed back to the caller: without it
    // a broken commit is indistinguishable from a broken tool.
    // ========================================================

    BuildArtifacts artifacts;

    if (
        !prepare_build(
            commit,
            artifacts,
            result.error
        )
    ) {
        result.build_log =
            artifacts.build_log;

        release_build(artifacts);

        return result;
    }

    // ========================================================
    // Step 5
    // Run benchmark
    // ========================================================

    Stats stats;

    if (
        !measure(
            artifacts.executable,
            calls,
            warmup,
            stats,
            result.error
        )
    ) {
        release_build(artifacts);

        return result;
    }

    result.current_us =
        stats.median_us;

    result.samples =
        stats.samples;

    result.stats =
        stats;

    // ========================================================
    // Step 6
    // Calculate performance change
    // ========================================================

    result.change_percent =
        calculate_change(
            result.baseline_us,
            result.current_us
        );

    // ========================================================
    // Step 7
    // Determine regression
    // ========================================================

    result.regression =
        result.change_percent >
        result.threshold_percent;

    // ========================================================
    // Step 8
    // Cleanup
    // ========================================================

    release_build(artifacts);

    // ========================================================
    // Step 9
    // Success
    // ========================================================

    result.success =
        true;

    return result;
}
