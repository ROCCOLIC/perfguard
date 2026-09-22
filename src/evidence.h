#pragma once

#include <string>
#include <vector>

struct EvidenceReport {
    std::string commit;
    std::string short_commit;
    std::string message;
    std::string author;

    std::vector<std::string> changed_files;

    int lines_added = 0;
    int lines_removed = 0;

    std::vector<std::string> performance_evidence;

    bool success = false;
    std::string error;
};

EvidenceReport analyze_commit(const std::string& commit);