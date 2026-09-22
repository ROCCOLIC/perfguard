#include "evidence.h"

#include <array>
#include <cstdio>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace {

std::string run_git_command(const std::string& command)
{
    std::array<char, 4096> buffer{};
    std::string result;

    FILE* pipe = _popen(command.c_str(), "r");

    if (pipe == nullptr) {
        return {};
    }

    while (fgets(buffer.data(),
                 static_cast<int>(buffer.size()),
                 pipe) != nullptr)
    {
        result += buffer.data();
    }

    _pclose(pipe);

    return result;
}

bool is_valid_commit_hash(const std::string& commit)
{
    if (commit.empty()) {
        return false;
    }

    static const std::regex hash_pattern(
        "^[0-9a-fA-F]{7,64}$"
    );

    return std::regex_match(commit, hash_pattern);
}

void analyze_added_line(
    const std::string& line,
    EvidenceReport& report)
{
    if (line.empty()) {
        return;
    }

    if (line[0] != '+') {
        return;
    }

    // Ignore diff header.
    if (line.rfind("+++", 0) == 0) {
        return;
    }

    const std::string code = line.substr(1);

    static const std::vector<std::string> keywords = {
        "sleep",
        "sleep_for",
        "chrono",
        "sort(",
        "stable_sort",
        "unordered_map",
        "map<",
        "vector<",
        "regex",
        "find(",
        "find_if",
        "for_each",
        "memcpy",
        "memmove",
        "filesystem"
    };

    for (const auto& keyword : keywords) {
        if (code.find(keyword) != std::string::npos) {
            report.performance_evidence.push_back(line);
            return;
        }
    }
}

} // namespace

EvidenceReport analyze_commit(const std::string& commit)
{
    EvidenceReport report;

    report.commit = commit;

    if (!is_valid_commit_hash(commit)) {
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
        run_git_command(metadata_command);

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
        run_git_command(files_command);

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
        run_git_command(diff_command);

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

            analyze_added_line(
                line,
                report
            );
        }
        else if (!line.empty() && line[0] == '-') {
            ++report.lines_removed;
        }
    }

    report.success = true;

    return report;
}