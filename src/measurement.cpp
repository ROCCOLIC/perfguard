#include "measurement.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "core.h"
#include "perfguard_config.h"

using namespace std;

namespace fs = std::filesystem;

namespace perfguard {

namespace {

// Below this many samples the outlier criterion has no statistical
// footing, so it is skipped rather than applied and trusted.
constexpr size_t MIN_SAMPLES_FOR_OUTLIER_CHECK = 7;

// A normal distribution has sigma = 1.4826 * MAD, so a distance of
// three sigma is that many MADs.
constexpr double MAD_TO_SIGMA = 1.4826;
constexpr double OUTLIER_SIGMA = 3.0;

void remove_directory(
    const fs::path& path
)
{
    error_code error;

    fs::remove_all(
        path,
        error
    );
}


// Each build gets directories of its own. Naming them after the
// commit alone is not enough: comparing a commit against itself is an
// ordinary thing to ask for, and both builds would then want the same
// two paths.
unsigned next_build_serial()
{
    static unsigned serial = 0;

    return ++serial;
}


// Git keeps a worktree registered even after its directory has
// disappeared, so stale registrations are pruned before a worktree is
// added and pruned again once it has been removed.
void prune_worktrees(
    const fs::path& project_root
)
{
    run_command(
        "git -C \"" +
        project_root.string() +
        "\" worktree prune",
        false
    );
}


bool worktree_registered(
    const fs::path& project_root,
    const fs::path& worktree
)
{
    const string name =
        worktree.filename().string();

    if (
        name.empty()
    ) {
        return false;
    }

    // The listing is searched, so stderr must not be merged into it.
    const string listing =
        run_command(
            "git -C \"" +
            project_root.string() +
            "\" worktree list --porcelain",
            false
        ).output;

    // Matched on the final path component, because git prints its
    // paths with forward slashes while std::filesystem gives back the
    // platform's native separators. Comparing whole paths therefore
    // never matched, and a registration whose directory had already
    // been deleted went unnoticed -- which is exactly the state git
    // refuses to add a worktree over.
    istringstream stream(listing);
    string line;

    while (
        getline(
            stream,
            line
        )
    ) {
        while (
            !line.empty() &&
            (
                line.back() == '\r' ||
                line.back() == '\n'
            )
        ) {
            line.pop_back();
        }

        if (
            line.size() < name.size()
        ) {
            continue;
        }

        if (
            line.compare(
                line.size() - name.size(),
                name.size(),
                name
            ) == 0
        ) {
            return true;
        }
    }

    return false;
}


double median_of(
    vector<double> values
)
{
    sort(
        values.begin(),
        values.end()
    );

    const size_t middle =
        values.size() / 2;

    if (
        values.size() % 2 != 0
    ) {
        return values[middle];
    }

    return
        (
            values[middle - 1] +
            values[middle]
        )
        / 2.0;
}


double mean_of(
    const vector<double>& values
)
{
    if (
        values.empty()
    ) {
        return 0.0;
    }

    double total = 0.0;

    for (
        double value :
        values
    ) {
        total += value;
    }

    return
        total /
        static_cast<double>(values.size());
}


double sample_stdev(
    const vector<double>& values
)
{
    if (
        values.size() < 2
    ) {
        return 0.0;
    }

    const double mean =
        mean_of(values);

    double total = 0.0;

    for (
        double value :
        values
    ) {
        const double deviation =
            value - mean;

        total +=
            deviation * deviation;
    }

    return
        sqrt(
            total /
            static_cast<double>(values.size() - 1)
        );
}


bool collect_samples(
    const fs::path& executable,
    int calls,
    vector<double>& samples,
    string& error
)
{
    if (
        calls <= 0
    ) {
        error =
            "Sample count must be positive.";

        return false;
    }

    // The average is parsed out of this output, so stderr must not be
    // merged into it.
    const string command =
        "\"" +
        executable.string() +
        "\"";

    for (
        int call = 0;
        call < calls;
        ++call
    ) {
        const CommandResult result =
            run_command(
                command,
                false,
                BENCHMARK_TIMEOUT_MS
            );

        if (
            result.timed_out
        ) {
            error =
                "The benchmark timed out.";

            return false;
        }

        double value = 0.0;

        if (
            !parse_average(
                result.output,
                value
            )
        ) {
            error =
                "Unable to parse the benchmark average.";

            return false;
        }

        samples.push_back(value);
    }

    return true;
}

}  // namespace


// ============================================================
// Build
// ============================================================

bool prepare_build(
    const string& commit,
    BuildArtifacts& artifacts,
    string& error
)
{
    if (
        !is_valid_commit_hash(commit)
    ) {
        error =
            "Invalid commit hash.";

        return false;
    }

    const fs::path project_root =
        find_project_root();

    if (
        project_root.empty()
    ) {
        error =
            "Unable to find the PerfGuard project root. "
            "Make sure the command is executed inside "
            "the PerfGuard project directory.";

        return false;
    }

    artifacts = BuildArtifacts{};

    artifacts.project_root =
        project_root;

    const string label =
        commit +
        "-" +
        to_string(next_build_serial());

    artifacts.worktree =
        session_temp_directory() /
        (
            "worktree-" +
            label
        );

    artifacts.build_dir =
        session_temp_directory() /
        (
            "build-" +
            label
        );

    prune_worktrees(
        project_root
    );

    if (
        worktree_registered(
            project_root,
            artifacts.worktree
        )
    ) {
        run_command(
            "git -C \"" +
            project_root.string() +
            "\" worktree remove --force \"" +
            artifacts.worktree.string() +
            "\"",
            false
        );
    }

    remove_directory(
        artifacts.worktree
    );

    remove_directory(
        artifacts.build_dir
    );

    // --------------------------------------------------------
    // Worktree
    // --------------------------------------------------------

    const CommandResult worktree_add =
        run_command(
            "git -C \"" +
            project_root.string() +
            "\" worktree add "
            "--detach \"" +
            artifacts.worktree.string() +
            "\" " +
            commit,
            false
        );

    if (
        worktree_add.exit_code != 0
    ) {
        artifacts.build_log =
            worktree_add.output;

        error =
            "Failed to create a Git worktree for " +
            commit +
            ".";

        release_build(artifacts);

        return false;
    }

    // --------------------------------------------------------
    // Configure
    // --------------------------------------------------------

    const CommandResult configure =
        run_command(
            "cmake "
            "-S \"" +
            artifacts.worktree.string() +
            "\" "
            "-B \"" +
            artifacts.build_dir.string() +
            "\" "
            "-G \"Visual Studio 17 2022\"",
            true,
            BUILD_TIMEOUT_MS
        );

    artifacts.build_log =
        configure.output;

    if (
        configure.exit_code != 0
    ) {
        error =
            "CMake configuration failed for " +
            commit +
            ".";

        release_build(artifacts);

        return false;
    }

    // --------------------------------------------------------
    // Build
    // --------------------------------------------------------

    const CommandResult build =
        run_command(
            "cmake --build \"" +
            artifacts.build_dir.string() +
            "\" --config Debug",
            true,
            BUILD_TIMEOUT_MS
        );

    artifacts.build_log =
        build.output;

    if (
        build.exit_code != 0
    ) {
        error =
            "The build failed for " +
            commit +
            ".";

        release_build(artifacts);

        return false;
    }

    // --------------------------------------------------------
    // Executable
    // --------------------------------------------------------

    artifacts.executable =
        artifacts.build_dir /
        "Debug" /
        BENCHMARK_EXECUTABLE_NAME;

    if (
        !fs::exists(
            artifacts.executable
        )
    ) {
        error =
            "The benchmark executable was not found after building " +
            commit +
            ".";

        release_build(artifacts);

        return false;
    }

    return true;
}


void release_build(
    BuildArtifacts& artifacts
)
{
    if (
        artifacts.worktree.empty()
    ) {
        return;
    }

    const fs::path project_root =
        artifacts.project_root;

    if (
        !project_root.empty() &&
        worktree_registered(
            project_root,
            artifacts.worktree
        )
    ) {
        run_command(
            "git -C \"" +
            project_root.string() +
            "\" worktree remove --force \"" +
            artifacts.worktree.string() +
            "\"",
            false
        );
    }

    remove_directory(
        artifacts.build_dir
    );

    remove_directory(
        artifacts.worktree
    );

    if (
        !project_root.empty()
    ) {
        prune_worktrees(
            project_root
        );
    }

    artifacts.worktree.clear();
    artifacts.build_dir.clear();
    artifacts.executable.clear();

    // build_log is deliberately kept: a caller that is unwinding from
    // a failure is the one that needs to show it.
}


// ============================================================
// Statistics
// ============================================================

Stats aggregate(
    const vector<double>& samples
)
{
    Stats stats;

    if (
        samples.empty()
    ) {
        return stats;
    }

    vector<double> values =
        samples;

    if (
        values.size() >=
        MIN_SAMPLES_FOR_OUTLIER_CHECK
    ) {
        stats.outlier_check =
            true;

        const double median =
            median_of(values);

        vector<double> deviations;

        for (
            double value :
            values
        ) {
            deviations.push_back(
                fabs(
                    value - median
                )
            );
        }

        const double mad =
            median_of(deviations);

        if (
            mad > 0.0
        ) {
            const double limit =
                OUTLIER_SIGMA /
                MAD_TO_SIGMA *
                mad;

            // At most the single most distant sample is removed.
            //
            // Measured shape of these samples: a tight cluster with a
            // heavy right tail -- an isolated run several percent high
            // -- which is what one removal is for. Removing freely
            // also deletes a whole mode when the samples happen to be
            // bimodal, because the median then falls between the modes
            // and the MAD collapses to the spread within one of them.
            // That moved the reported median by nearly a percent
            // between identical runs, which is the size of the effect
            // under investigation, so it is not allowed to happen.
            size_t most_distant = values.size();
            double largest_distance = 0.0;

            for (
                size_t index = 0;
                index < values.size();
                ++index
            ) {
                const double distance =
                    fabs(
                        values[index] -
                        median
                    );

                if (
                    distance > largest_distance
                ) {
                    largest_distance =
                        distance;

                    most_distant =
                        index;
                }
            }

            if (
                most_distant < values.size() &&
                largest_distance > limit
            ) {
                values.erase(
                    values.begin() +
                    static_cast<ptrdiff_t>(most_distant)
                );

                stats.outliers_removed = 1;
            }
        }
    }

    stats.samples =
        static_cast<int>(values.size());

    stats.min_us =
        *min_element(
            values.begin(),
            values.end()
        );

    stats.max_us =
        *max_element(
            values.begin(),
            values.end()
        );

    stats.median_us =
        median_of(values);

    // Recorded for backwards compatibility with the average_us field a
    // version 1 baseline stored; the median is the figure that decides
    // a verdict.
    stats.mean_us =
        mean_of(values);

    stats.stdev_us =
        sample_stdev(values);

    return stats;
}


// ============================================================
// Measurement
// ============================================================

bool measure(
    const fs::path& executable,
    int calls,
    int warmup,
    Stats& stats,
    string& error
)
{
    if (
        !fs::exists(executable)
    ) {
        error =
            "Benchmark executable not found: " +
            executable.string();

        return false;
    }

    if (
        warmup > 0
    ) {
        vector<double> discarded;

        if (
            !collect_samples(
                executable,
                warmup,
                discarded,
                error
            )
        ) {
            return false;
        }
    }

    vector<double> samples;

    if (
        !collect_samples(
            executable,
            calls,
            samples,
            error
        )
    ) {
        return false;
    }

    stats =
        aggregate(samples);

    return true;
}


bool measure_interleaved(
    const fs::path& executable_a,
    const fs::path& executable_b,
    int rounds,
    int calls,
    Stats& stats_a,
    Stats& stats_b,
    string& error
)
{
    if (
        !fs::exists(executable_a)
    ) {
        error =
            "Benchmark executable not found: " +
            executable_a.string();

        return false;
    }

    if (
        !fs::exists(executable_b)
    ) {
        error =
            "Benchmark executable not found: " +
            executable_b.string();

        return false;
    }

    if (
        rounds <= 0 ||
        calls <= 0
    ) {
        error =
            "Round and sample counts must be positive.";

        return false;
    }

    // Settle both before measuring either.
    vector<double> discarded;

    if (
        !collect_samples(
            executable_a,
            1,
            discarded,
            error
        ) ||
        !collect_samples(
            executable_b,
            1,
            discarded,
            error
        )
    ) {
        return false;
    }

    vector<double> samples_a;
    vector<double> samples_b;

    for (
        int round = 0;
        round < rounds;
        ++round
    ) {
        if (
            !collect_samples(
                executable_a,
                calls,
                samples_a,
                error
            ) ||
            !collect_samples(
                executable_b,
                calls,
                samples_b,
                error
            )
        ) {
            return false;
        }
    }

    stats_a =
        aggregate(samples_a);

    stats_b =
        aggregate(samples_b);

    return true;
}

}  // namespace perfguard
