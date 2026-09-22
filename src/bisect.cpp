#include "bisect.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string run_command(
    const std::string& command,
    int& exit_code
)
{
    std::array<char, 4096> buffer{};
    std::string output;

    FILE* pipe =
        _popen(
            command.c_str(),
            "r"
        );

    if (pipe == nullptr) {
        exit_code = -1;
        return {};
    }

    while (
        fgets(
            buffer.data(),
            static_cast<int>(buffer.size()),
            pipe
        ) != nullptr
    ) {
        output += buffer.data();
    }

    exit_code =
        _pclose(pipe);

    return output;
}

bool read_baseline_commit(
    const fs::path& baseline_path,
    std::string& commit
)
{
    std::ifstream file(
        baseline_path
    );

    if (!file) {
        return false;
    }

    std::stringstream buffer;

    buffer << file.rdbuf();

    const std::string content =
        buffer.str();

    static const std::regex pattern(
        R"PG("commit"\s*:\s*"([^"]+)")PG"
    );

    std::smatch match;

    if (
        !std::regex_search(
            content,
            match,
            pattern
        )
    ) {
        return false;
    }

    commit =
        match[1].str();

    return true;
}

bool is_valid_commit(
    const std::string& commit
)
{
    static const std::regex pattern(
        "^[0-9a-fA-F]{7,64}$"
    );

    return std::regex_match(
        commit,
        pattern
    );
}

std::string find_project_root()
{
    fs::path current =
        fs::current_path();

    while (true) {

        if (
            fs::exists(
                current / ".git"
            ) &&
            fs::exists(
                current / ".perfguard"
            )
        ) {
            return current.string();
        }

        const fs::path parent =
            current.parent_path();

        if (parent == current) {
            break;
        }

        current = parent;
    }

    return {};
}

std::string extract_first_bad_commit(
    const std::string& output
)
{
    static const std::regex pattern(
        R"PG(([0-9a-fA-F]{7,64}) is the first bad commit)PG"
    );

    std::smatch match;

    if (
        std::regex_search(
            output,
            match,
            pattern
        )
    ) {
        return match[1].str();
    }

    return {};
}

} // namespace

BisectResult run_bisect()
{
    BisectResult result;

    const std::string project_root =
        find_project_root();

    if (project_root.empty()) {
        result.error =
            "Unable to find PerfGuard project root.";

        return result;
    }

    const fs::path baseline_path =
        fs::path(project_root)
        / ".perfguard"
        / "baseline.json";

    if (
        !read_baseline_commit(
            baseline_path,
            result.baseline_commit
        )
    ) {
        result.error =
            "Unable to read baseline commit.";

        return result;
    }

    if (
        !is_valid_commit(
            result.baseline_commit
        )
    ) {
        result.error =
            "Baseline contains an invalid commit.";

        return result;
    }

    int exit_code = 0;

    const std::string check_script =
        (
            fs::path(project_root)
            / "tools"
            / "bisect_check.ps1"
        ).string();

    if (
        !fs::exists(
            check_script
        )
    ) {
        result.error =
            "bisect_check.ps1 was not found.";

        return result;
    }

    const std::string git_root =
        "git -C \"" +
        project_root +
        "\" ";

    result.output +=
        "========================================\n"
        "PerfGuard Automatic Bisect\n"
        "========================================\n\n";

    result.output +=
        "Baseline commit: " +
        result.baseline_commit +
        "\n\n";

    /*
     * Make sure a previous bisect session
     * does not interfere with this run.
     */
    run_command(
        git_root + "bisect reset",
        exit_code
    );

    /*
     * Start bisect.
     */
    const std::string start_output =
        run_command(
            git_root + "bisect start",
            exit_code
        );

    result.output +=
        start_output;

    if (exit_code != 0) {
        result.error =
            "Failed to start git bisect.";

        return result;
    }

    /*
     * Current HEAD is considered bad.
     */
    const std::string bad_output =
        run_command(
            git_root + "bisect bad HEAD",
            exit_code
        );

    result.output +=
        bad_output;

    if (exit_code != 0) {

        run_command(
            git_root + "bisect reset",
            exit_code
        );

        result.error =
            "Failed to mark HEAD as bad.";

        return result;
    }

    /*
     * Baseline is considered good.
     */
    const std::string good_output =
        run_command(
            git_root +
            "bisect good " +
            result.baseline_commit,
            exit_code
        );

    result.output +=
        good_output;

    if (exit_code != 0) {

        run_command(
            git_root + "bisect reset",
            exit_code
        );

        result.error =
            "Failed to mark baseline as good.";

        return result;
    }

    /*
     * Let Git automatically choose commits
     * and execute the performance test.
     */
    const std::string command =
        git_root +
        "bisect run powershell "
        "-ExecutionPolicy Bypass "
        "-File \"" +
        check_script +
        "\"";

    const std::string bisect_output =
        run_command(
            command,
            exit_code
        );

    result.output +=
        bisect_output;

    /*
     * Always leave the repository outside
     * of an active bisect session.
     */
    int reset_code = 0;

    const std::string reset_output =
        run_command(
            git_root + "bisect reset",
            reset_code
        );

    result.output +=
        reset_output;

    result.first_bad_commit =
        extract_first_bad_commit(
            bisect_output
        );

    if (
        result.first_bad_commit.empty()
    ) {
        result.error =
            "Git bisect did not report a first bad commit.";

        return result;
    }

    result.success = true;

    return result;
}