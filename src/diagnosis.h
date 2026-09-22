#pragma once

#include <string>

struct DiagnosisResult {
    bool success = false;
    bool regression = false;

    std::string commit;

    double baseline_us = 0.0;
    double current_us = 0.0;
    double change_percent = 0.0;
    double threshold_percent = 10.0;

    std::string error;
};

DiagnosisResult diagnose_commit(
    const std::string& commit
);