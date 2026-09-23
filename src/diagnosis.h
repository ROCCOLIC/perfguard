#pragma once

#include <string>

#include "measurement.h"
#include "perfguard_config.h"

struct DiagnosisResult {
    bool success = false;
    bool regression = false;

    std::string commit;

    double baseline_us = 0.0;
    double current_us = 0.0;
    double change_percent = 0.0;
    double threshold_percent = perfguard::DEFAULT_THRESHOLD_PERCENT;

    // Samples the figure it reports the median of.
    int samples = 0;

    // The whole measurement, so a caller can report its spread rather
    // than just the median it compares.
    perfguard::Stats stats;

    std::string error;

    // Compiler output from the failed build, empty when the build
    // succeeded. A caller that reports a failure is the one that
    // needs to show why it happened.
    std::string build_log;
};

DiagnosisResult diagnose_commit(
    const std::string& commit,
    int calls = 1,
    int warmup = 0,
    double threshold_percent =
        perfguard::DEFAULT_THRESHOLD_PERCENT
);