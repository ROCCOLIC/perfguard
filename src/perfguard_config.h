#pragma once


// Shared by the PerfGuard tool and the standalone benchmark so that
// the two targets cannot drift apart. Header-only by design: a shared
// translation unit would force sort_1m to link the tool's Windows code.
namespace perfguard {

inline constexpr int BENCHMARK_SIZE = 1'000'000;
inline constexpr int BENCHMARK_RUNS = 10;

inline constexpr const char* BENCHMARK_NAME = "sort_1m";
inline constexpr const char* BENCHMARK_EXECUTABLE_NAME = "sort_1m.exe";

// How long the regression fixture sleeps inside the timed region. It
// has to clear the threshold by a wide margin: the measurement noise
// between two build directories is a few percent, so a slowdown near
// the threshold produces a verdict that flips between runs.
inline constexpr int FIXTURE_SLEEP_MS = 50;

inline constexpr double DEFAULT_THRESHOLD_PERCENT = 10.0;


// --check builds and measures both sides in one session, which is why
// it takes minutes rather than seconds: each sample is a whole
// benchmark run, so this is 2 * (1 + samples) of them, on top of two
// full builds. The two sides are taken alternately, one sample each.
inline constexpr int CHECK_SAMPLES_PER_SIDE = 9;

// Samples a baseline is measured from. --check does not read the
// stored number -- it builds and measures both sides itself -- so this
// sizes only what --report and --diagnose compare against, plus the
// spread recorded next to it.
inline constexpr int BASELINE_SAMPLES = 5;


// Command timeouts. A child process that never exits must never hang
// PerfGuard, so every command is run with a deadline. The default is
// the git deadline, because that is what almost every call is.
inline constexpr int GIT_TIMEOUT_MS = 30'000;
inline constexpr int BUILD_TIMEOUT_MS = 120'000;
inline constexpr int BENCHMARK_TIMEOUT_MS = 300'000;

}  // namespace perfguard
