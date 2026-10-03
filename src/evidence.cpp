#include "evidence.h"

#include <iostream>
#include <regex>
#include <sstream>
#include <string>

#include "core.h"

namespace {

// A line of a diff holds the path when it holds nothing else: an
// added or removed line begins with "+" or "-", and the "+++"/"---"
// header is caught before this by analyze_added_line's own check.
std::string file_header_path(const std::string& line)
{
    static const std::regex pattern(
        R"((?:^|\s)(?:a|b)/(.+)$)"
    );

    std::smatch match;

    if (!std::regex_search(line, match, pattern)) {
        return {};
    }

    return match[1].str();
}

}  // namespace

bool is_benchmark_source(const std::string& path)
{
    // Separators are compared as one character, because a diff names
    // files with forward slashes and a path handed in by a caller may
    // not.
    std::string normalized = path;

    for (char& character : normalized) {
        if (character == '\\') {
            character = '/';
        }
    }

    // Only a whole segment named "test" or "tests" is excluded, so a
    // file whose name merely begins with the letters -- "testable.cpp"
    // -- is still scanned, and only a test directory is skipped.
    std::istringstream segments(normalized);

    std::string segment;

    while (std::getline(segments, segment, '/')) {
        if (segment == "test" || segment == "tests") {
            return false;
        }
    }

    return true;
}

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

    bool scanning = true;

    while (std::getline(diff_stream, line)) {

        // The "+++ b/path" header names the file the following lines
        // belong to. A hunk header or an added line can hold the same
        // "b/" text inside a string, so only a line that holds the
        // path and nothing else moves the scan to another file.
        if (line.rfind("+++ ", 0) == 0 ||
            line.rfind("--- ", 0) == 0)
        {
            const std::string path =
                file_header_path(line);

            if (!path.empty()) {
                scanning =
                    is_benchmark_source(path);
            }

            continue;
        }

        if (line.rfind("diff --git ", 0) == 0) {
            scanning = true;

            continue;
        }

        if (!line.empty() && line[0] == '+') {
            ++report.lines_added;

            if (scanning) {
                std::string evidence;

                if (perfguard::analyze_added_line(line, evidence)) {
                    report.performance_evidence.push_back(evidence);
                }
            }
        }
        else if (!line.empty() && line[0] == '-') {
            ++report.lines_removed;
        }
    }

    report.success = true;

    return report;
}
