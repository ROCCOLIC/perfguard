
#include "diagnosis.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <regex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// ============================================================
// Configuration
// ============================================================

const double REGRESSION_THRESHOLD = 10.0;

// ============================================================
// Execute command
// ============================================================

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

// ============================================================
// Project root detection
//
// Starting from the current working directory, walk upward
// until we find both:
//
//     .git
//     .perfguard
//
// This makes PerfGuard independent of the installation path.
// ============================================================

bool is_project_root(
    const fs::path& path
)
{
    return
        fs::exists(
            path / ".git"
        )
        &&
        fs::exists(
            path / ".perfguard"
        );
}

fs::path find_project_root()
{
    std::error_code error;

    fs::path current =
        fs::current_path(
            error
        );

    if (error) {
        return {};
    }

    while (true) {

        if (
            is_project_root(
                current
            )
        ) {
            return current;
        }

        fs::path parent =
            current.parent_path();

        // Reached filesystem root.

        if (
            parent == current
        ) {
            break;
        }

        current =
            parent;
    }

    return {};
}

// ============================================================
// Commit validation
// ============================================================

bool is_valid_commit(
    const std::string& commit
)
{
    if (
        commit.empty()
    ) {
        return false;
    }

    static const std::regex pattern(
        "^[0-9a-fA-F]{7,64}$"
    );

    return std::regex_match(
        commit,
        pattern
    );
}

// ============================================================
// Remove directory
// ============================================================

void remove_directory(
    const fs::path& path
)
{
    std::error_code error;

    fs::remove_all(
        path,
        error
    );
}

// ============================================================
// Cleanup temporary resources
// ============================================================

void cleanup(
    const fs::path& project_root,
    const fs::path& worktree,
    const fs::path& build_dir
)
{
    // --------------------------------------------------------
    // Remove build directory
    // --------------------------------------------------------

    remove_directory(
        build_dir
    );

    // --------------------------------------------------------
    // Check whether the diagnostic directory is registered
    // as a Git worktree.
    // --------------------------------------------------------

    const std::string list_command =
        "git -C \"" +
        project_root.string() +
        "\" "
        "worktree list --porcelain";

    int list_exit_code = 0;

    const std::string worktree_list =
        run_command(
            list_command,
            list_exit_code
        );

    const bool worktree_exists =
        worktree_list.find(
            worktree.string()
        )
        != std::string::npos;

    // --------------------------------------------------------
    // Remove registered Git worktree
    // --------------------------------------------------------

    if (
        worktree_exists
    ) {

        const std::string remove_command =
            "git -C \"" +
            project_root.string() +
            "\" "
            "worktree remove "
            "--force \"" +
            worktree.string() +
            "\"";

        int remove_exit_code = 0;

        run_command(
            remove_command,
            remove_exit_code
        );
    }

    // --------------------------------------------------------
    // Make absolutely sure the directory is gone.
    // --------------------------------------------------------

    remove_directory(
        worktree
    );
}

// ============================================================
// Read baseline
// ============================================================

bool read_baseline(
    const fs::path& project_root,
    double& baseline
)
{
    const fs::path baseline_file =
        project_root /
        ".perfguard" /
        "baseline.json";

    FILE* file =
        fopen(
            baseline_file.string().c_str(),
            "r"
        );

    if (
        file == nullptr
    ) {
        return false;
    }

    char buffer[4096];

    std::string content;

    while (
        fgets(
            buffer,
            sizeof(buffer),
            file
        ) != nullptr
    ) {
        content += buffer;
    }

    fclose(
        file
    );

    // --------------------------------------------------------
    // Current baseline format:
    //
    // {
    //     "benchmark": "sort_1m",
    //     "commit": "e6ba07d",
    //     "average_us": 62738.50
    // }
    // --------------------------------------------------------

    static const std::regex pattern(
        R"("average_us"\s*:\s*([0-9.]+))"
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

    try {

        baseline =
            std::stod(
                match[1].str()
            );

    }
    catch (...) {

        return false;
    }

    return baseline > 0.0;
}

// ============================================================
// Parse benchmark average
// ============================================================

bool parse_average(
    const std::string& output,
    double& average
)
{
    std::istringstream stream(
        output
    );

    std::string line;

    while (
        std::getline(
            stream,
            line
        )
    ) {

        if (
            line.find(
                "Average:"
            )
            == std::string::npos
        ) {
            continue;
        }

        static const std::regex pattern(
            R"(Average:\s+([0-9.]+)\s+us)"
        );

        std::smatch match;

        if (
            std::regex_search(
                line,
                match,
                pattern
            )
        ) {

            try {

                average =
                    std::stod(
                        match[1].str()
                    );

            }
            catch (...) {

                return false;
            }

            return true;
        }
    }

    return false;
}

} // namespace

// ============================================================
// Diagnose commit
// ============================================================

DiagnosisResult diagnose_commit(
    const std::string& commit
)
{
    DiagnosisResult result;

    result.commit =
        commit;

    result.threshold_percent =
        REGRESSION_THRESHOLD;

    // ========================================================
    // Step 1
    // Validate commit
    // ========================================================

    if (
        !is_valid_commit(
            commit
        )
    ) {

        result.error =
            "Invalid commit hash.";

        return result;
    }

    // ========================================================
    // Step 2
    // Find project root
    // ========================================================

    const fs::path project_root =
        find_project_root();

    if (
        project_root.empty()
    ) {

        result.error =
            "Unable to find PerfGuard project root. "
            "Make sure the command is executed inside "
            "the PerfGuard project directory.";

        return result;
    }

    // ========================================================
    // Step 3
    // Read baseline
    // ========================================================

    double baseline = 0.0;

    if (
        !read_baseline(
            project_root,
            baseline
        )
    ) {

        result.error =
            "Unable to read "
            ".perfguard/baseline.json.";

        return result;
    }

    result.baseline_us =
        baseline;

    // ========================================================
    // Step 4
    // Create temporary paths
    //
    // Example:
    //
    // C:\Users\<user>\AppData\Local\Temp\
    //     perfguard-diagnose
    //
    // C:\Users\<user>\AppData\Local\Temp\
    //     perfguard-diagnose-build
    // ========================================================

    fs::path temp_root;

    try {

        temp_root =
            fs::temp_directory_path();

    }
    catch (...) {

        result.error =
            "Unable to determine system temporary directory.";

        return result;
    }

    const fs::path worktree =
        temp_root /
        "perfguard-diagnose";

    const fs::path build_dir =
        temp_root /
        "perfguard-diagnose-build";

    // ========================================================
    // Step 5
    // Cleanup previous temporary state
    // ========================================================

    cleanup(
        project_root,
        worktree,
        build_dir
    );

    // ========================================================
    // Step 6
    // Create Git worktree
    // ========================================================

    {
        const std::string command =
            "git -C \"" +
            project_root.string() +
            "\" "
            "worktree add "
            "--detach \"" +
            worktree.string() +
            "\" " +
            commit;

        int exit_code = 0;

        run_command(
            command,
            exit_code
        );

        if (
            exit_code != 0
        ) {

            cleanup(
                project_root,
                worktree,
                build_dir
            );

            result.error =
                "Failed to create Git worktree.";

            return result;
        }
    }

    // ========================================================
    // Step 7
    // Configure target commit
    // ========================================================

    {
        const std::string command =
            "cmake "
            "-S \"" +
            worktree.string() +
            "\" "
            "-B \"" +
            build_dir.string() +
            "\" "
            "-G \"Visual Studio 17 2022\"";

        int exit_code = 0;

        run_command(
            command,
            exit_code
        );

        if (
            exit_code != 0
        ) {

            cleanup(
                project_root,
                worktree,
                build_dir
            );

            result.error =
                "CMake configuration failed.";

            return result;
        }
    }

    // ========================================================
    // Step 8
    // Build target commit
    // ========================================================

    {
        const std::string command =
            "cmake "
            "--build \"" +
            build_dir.string() +
            "\" "
            "--config Debug";

        int exit_code = 0;

        run_command(
            command,
            exit_code
        );

        if (
            exit_code != 0
        ) {

            cleanup(
                project_root,
                worktree,
                build_dir
            );

            result.error =
                "Target commit build failed.";

            return result;
        }
    }

    // ========================================================
    // Step 9
    // Find executable
    // ========================================================

    const fs::path executable =
        build_dir /
        "Debug" /
        "perfguard.exe";

    if (
        !fs::exists(
            executable
        )
    ) {

        cleanup(
            project_root,
            worktree,
            build_dir
        );

        result.error =
            "Target executable was not found.";

        return result;
    }

    // ========================================================
    // Step 10
    // Run benchmark
    // ========================================================

    std::string output;

    {
        const std::string command =
            "\"" +
            executable.string() +
            "\"";

        int exit_code = 0;

        output =
            run_command(
                command,
                exit_code
            );

        if (
            exit_code != 0
        ) {

            cleanup(
                project_root,
                worktree,
                build_dir
            );

            result.error =
                "Target benchmark failed.";

            return result;
        }
    }

    // ========================================================
    // Step 11
    // Parse benchmark average
    // ========================================================

    double current = 0.0;

    if (
        !parse_average(
            output,
            current
        )
    ) {

        cleanup(
            project_root,
            worktree,
            build_dir
        );

        result.error =
            "Unable to parse benchmark average.";

        return result;
    }

    result.current_us =
        current;

    // ========================================================
    // Step 12
    // Calculate performance change
    // ========================================================

    result.change_percent =
        (
            (
                result.current_us -
                result.baseline_us
            )
            /
            result.baseline_us
        )
        * 100.0;

    // ========================================================
    // Step 13
    // Determine regression
    // ========================================================

    result.regression =
        result.change_percent >
        result.threshold_percent;

    // ========================================================
    // Step 14
    // Cleanup
    // ========================================================

    cleanup(
        project_root,
        worktree,
        build_dir
    );

    // ========================================================
    // Step 15
    // Success
    // ========================================================

    result.success =
        true;

    return result;
}

