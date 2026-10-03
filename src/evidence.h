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

// Reports whether an added line of a path is one whose changes can
// move the benchmark.
//
// Test files are excluded: their assertions call the same functions a
// performance change is made of -- a find() on a string, a sort -- so
// scanning them named unrelated test lines as the cause of a
// regression.
bool is_benchmark_source(const std::string& path);
