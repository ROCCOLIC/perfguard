#include "evidence.h"

#include <iostream>
#include <regex>
#include <sstream>
#include <string>

#include "core.h"

EvidenceReport analyze_commit(const std::string& commit)
{
    EvidenceReport report;

    report.commit = commit;

    if (!perfguard::is_valid_commit_hash(commit)) {
        report.error =
            "Invalid commit hash. Expected 7-64 hexadecimal characters.";
        return report;
    }

    // --------------------------------------------------------
    // Commit metadata
    // --------------------------------------------------------

    const std::string metadata_command =
        "git show -s --format=\"%H%n%h%n%s%n%an\" " + commit;

    const std::string metadata =
        perfguard::run_command(metadata_command, false).output;

    if (metadata.empty()) {
        report.error =
            "Unable to read commit metadata.";
        return report;
    }

    std::istringstream metadata_stream(metadata);

    std::getline(
        metadata_stream,
        report.commit
    );

    std::getline(
        metadata_stream,
        report.short_commit
    );

    std::getline(
        metadata_stream,
        report.message
    );

    std::getline(
        metadata_stream,
        report.author
    );

    // --------------------------------------------------------
    // Changed files
    // --------------------------------------------------------

    const std::string files_command =
        "git diff-tree --no-commit-id --name-only -r " + commit;

    const std::string files =
        perfguard::run_command(files_command, false).output;

    std::istringstream file_stream(files);

    std::string file;

    while (std::getline(file_stream, file)) {
        if (!file.empty()) {
            report.changed_files.push_back(file);
        }
    }

    // --------------------------------------------------------
    // Diff
    // --------------------------------------------------------

    const std::string diff_command =
        "git show --format= --no-ext-diff " + commit;

    const std::string diff =
        perfguard::run_command(diff_command, false).output;

    if (diff.empty()) {
        report.error =
            "Unable to read commit diff.";
        return report;
    }

    std::istringstream diff_stream(diff);

    std::string line;

    while (std::getline(diff_stream, line)) {

        if (line.rfind("+++", 0) == 0 ||
            line.rfind("---", 0) == 0)
        {
            continue;
        }

        if (!line.empty() && line[0] == '+') {
            ++report.lines_added;

            std::string evidence;

            if (perfguard::analyze_added_line(line, evidence)) {
                report.performance_evidence.push_back(evidence);
            }
        }
        else if (!line.empty() && line[0] == '-') {
            ++report.lines_removed;
        }
    }

    report.success = true;

    return report;
}
