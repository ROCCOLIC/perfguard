#include "core.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace std;

namespace fs = std::filesystem;

namespace perfguard {

namespace {

// How often the reader wakes up to check the deadline while the child
// is producing no output. Small enough to react quickly, large enough
// not to spin.
constexpr DWORD POLL_INTERVAL_MS = 50;

constexpr size_t PIPE_BUFFER_SIZE = 4096;

bool is_usable_handle(
    HANDLE handle
)
{
    return
        handle != nullptr &&
        handle != INVALID_HANDLE_VALUE;
}

// Commands are built as narrow strings, which the CRT encodes in the
// active code page; paths from std::filesystem follow the same rule.
wstring widen(
    const string& value
)
{
    if (
        value.empty()
    ) {
        return {};
    }

    const int length =
        MultiByteToWideChar(
            CP_ACP,
            0,
            value.c_str(),
            static_cast<int>(value.size()),
            nullptr,
            0
        );

    if (
        length <= 0
    ) {
        return {};
    }

    wstring result(
        static_cast<size_t>(length),
        L'\0'
    );

    MultiByteToWideChar(
        CP_ACP,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        length
    );

    return result;
}

// Stands in for stdin and, when there is no console to write errors
// to, for stderr. The child gets an empty input stream instead of
// ours: a git command that decides to prompt would otherwise block
// until its deadline.
HANDLE open_null_device(
    SECURITY_ATTRIBUTES& security
)
{
    return CreateFileW(
        L"NUL",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        0,
        nullptr
    );
}

// Holds the session directory and removes it on the way out, so that
// no exit path has to remember to. The directories inside it are
// removed as they are finished with; this clears the shell they were
// created in.
struct SessionDirectory
{
    fs::path path;

    ~SessionDirectory()
    {
        error_code error;

        fs::remove_all(
            path,
            error
        );
    }
};

// Killing the shell alone can leave the build tools it started running
// and holding the build directory. A job object takes the whole tree
// down together.
HANDLE create_kill_on_close_job()
{
    HANDLE job =
        CreateJobObjectW(
            nullptr,
            nullptr
        );

    if (
        job == nullptr
    ) {
        return nullptr;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};

    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (
        !SetInformationJobObject(
            job,
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits)
        )
    ) {
        CloseHandle(job);

        return nullptr;
    }

    return job;
}

}  // namespace


CommandResult run_command(
    const string& command,
    bool capture_stderr,
    int timeout_ms
)
{
    CommandResult result;

    if (
        command.empty()
    ) {
        result.output =
            "Empty command.";

        return result;
    }

    SECURITY_ATTRIBUTES security{};

    security.nLength =
        sizeof(security);

    security.bInheritHandle =
        TRUE;

    security.lpSecurityDescriptor =
        nullptr;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;

    if (
        !CreatePipe(
            &read_pipe,
            &write_pipe,
            &security,
            0
        )
    ) {
        result.output =
            "Failed to create pipe for command: " +
            command;

        return result;
    }

    // The child must not inherit our read end, or the pipe never
    // reports end-of-file once the child exits.
    if (
        !SetHandleInformation(
            read_pipe,
            HANDLE_FLAG_INHERIT,
            0
        )
    ) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);

        result.output =
            "Failed to configure pipe for command: " +
            command;

        return result;
    }

    HANDLE null_device =
        open_null_device(
            security
        );

    // With the streams captured, the child's errors have nowhere to go
    // unless they share the pipe; without capture they belong on our
    // own console, exactly as they were before.
    HANDLE error_target =
        capture_stderr
            ? write_pipe
            : GetStdHandle(STD_ERROR_HANDLE);

    if (
        !is_usable_handle(
            error_target
        )
    ) {
        error_target =
            is_usable_handle(null_device)
                ? null_device
                : write_pipe;
    }

    STARTUPINFOW startup{};

    startup.cb =
        sizeof(startup);

    startup.dwFlags =
        STARTF_USESTDHANDLES;

    startup.hStdInput =
        is_usable_handle(null_device)
            ? null_device
            : nullptr;

    startup.hStdOutput =
        write_pipe;

    startup.hStdError =
        error_target;

    PROCESS_INFORMATION process_info{};

    // cmd.exe is kept because the existing command strings are written
    // for its parsing. Standard error is routed through handles rather
    // than "2>&1", so nothing needs the shell's redirection.
    const wstring command_line =
        L"cmd.exe /C " +
        widen(command);

    // CreateProcessW is allowed to modify the buffer, so it must own
    // writable storage.
    vector<wchar_t> mutable_command_line(
        command_line.begin(),
        command_line.end()
    );

    mutable_command_line.push_back(
        L'\0'
    );

    const BOOL created =
        CreateProcessW(
            nullptr,
            mutable_command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process_info
        );

    // Our copies of the inherited handles must go, whatever happens
    // next: holding the write end open would keep the pipe from ever
    // reaching end-of-file.
    CloseHandle(write_pipe);

    if (
        is_usable_handle(null_device)
    ) {
        CloseHandle(null_device);
    }

    if (
        !created
    ) {
        CloseHandle(read_pipe);

        result.output =
            "Failed to start command: " +
            command;

        return result;
    }

    HANDLE job =
        create_kill_on_close_job();

    if (
        job != nullptr &&
        !AssignProcessToJobObject(
            job,
            process_info.hProcess
        )
    ) {
        CloseHandle(job);

        job = nullptr;
    }

    array<char, PIPE_BUFFER_SIZE> buffer{};

    bool process_exited = false;

    const ULONGLONG start_tick =
        GetTickCount64();

    for (
        ;;
    ) {
        DWORD available = 0;

        if (
            !PeekNamedPipe(
                read_pipe,
                nullptr,
                0,
                nullptr,
                &available,
                nullptr
            )
        ) {
            // The child closed the pipe, so no output can follow.
            break;
        }

        if (
            available > 0
        ) {
            DWORD bytes_read = 0;

            if (
                !ReadFile(
                    read_pipe,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    &bytes_read,
                    nullptr
                ) ||
                bytes_read == 0
            ) {
                break;
            }

            result.output.append(
                buffer.data(),
                bytes_read
            );

            continue;
        }

        // Nothing buffered and the child is gone, so the run is over.
        if (
            process_exited
        ) {
            break;
        }

        if (
            WaitForSingleObject(
                process_info.hProcess,
                POLL_INTERVAL_MS
            ) == WAIT_OBJECT_0
        ) {
            // Loop once more to drain whatever it wrote on the way out.
            process_exited = true;

            continue;
        }

        if (
            GetTickCount64() - start_tick >=
            static_cast<ULONGLONG>(timeout_ms)
        ) {
            if (
                job != nullptr
            ) {
                TerminateJobObject(
                    job,
                    1
                );
            }
            else {
                TerminateProcess(
                    process_info.hProcess,
                    1
                );
            }

            WaitForSingleObject(
                process_info.hProcess,
                5'000
            );

            result.timed_out = true;

            // Drain the output produced before the kill, then stop.
            process_exited = true;

            continue;
        }
    }

    DWORD exit_code = 0;

    if (
        GetExitCodeProcess(
            process_info.hProcess,
            &exit_code
        )
    ) {
        result.exit_code =
            static_cast<int>(exit_code);
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    CloseHandle(read_pipe);

    if (
        job != nullptr
    ) {
        CloseHandle(job);
    }

    return result;
}


fs::path session_temp_directory()
{
    static const SessionDirectory session = [] {
        error_code error;

        fs::path root =
            fs::temp_directory_path(
                error
            );

        if (
            error ||
            root.empty()
        ) {
            root = ".";
        }

        const string name =
            "perfguard-" +
            to_string(
                GetCurrentProcessId()
            ) +
            "-" +
            to_string(
                chrono::duration_cast<chrono::milliseconds>(
                    chrono::system_clock::now()
                        .time_since_epoch()
                ).count()
            );

        const fs::path result =
            root / name;

        error.clear();

        fs::create_directories(
            result,
            error
        );

        return SessionDirectory{result};
    }();

    return session.path;
}


namespace {

bool is_project_root(
    const fs::path& path
)
{
    return
        fs::exists(path / ".git") &&
        fs::exists(path / "CMakeLists.txt");
}

}  // namespace


fs::path find_project_root()
{
    error_code error;

    fs::path current =
        fs::current_path(error);

    if (
        error
    ) {
        return {};
    }

    while (true) {

        if (
            is_project_root(current)
        ) {
            return current;
        }

        const fs::path parent =
            current.parent_path();

        if (
            parent == current
        ) {
            break;
        }

        current = parent;
    }

    return {};
}


string trim(
    string value
)
{
    while (
        !value.empty() &&
        (
            value.back() == '\n' ||
            value.back() == '\r' ||
            value.back() == ' ' ||
            value.back() == '\t'
        )
    ) {
        value.pop_back();
    }

    size_t start = 0;

    while (
        start < value.size() &&
        (
            value[start] == ' ' ||
            value[start] == '\t'
        )
    ) {
        ++start;
    }

    return value.substr(start);
}


bool is_valid_commit_hash(
    const string& commit
)
{
    if (
        commit.empty()
    ) {
        return false;
    }

    static const regex hash_pattern(
        "^[0-9a-fA-F]{7,64}$"
    );

    return regex_match(
        commit,
        hash_pattern
    );
}


bool parse_average(
    const string& output,
    double& average
)
{
    static const regex pattern(
        R"(Average:\s*([0-9]+(?:\.[0-9]+)?)\s*us)"
    );

    istringstream stream(
        output
    );

    string line;

    while (
        getline(
            stream,
            line
        )
    ) {
        smatch match;

        if (
            !regex_search(
                line,
                match,
                pattern
            )
        ) {
            continue;
        }

        try {
            average =
                stod(
                    match[1].str()
                );
        }
        catch (...) {
            return false;
        }

        return true;
    }

    return false;
}


double calculate_change(
    double baseline,
    double current
)
{
    if (
        baseline <= 0.0
    ) {
        return 0.0;
    }

    return
        (
            (
                current -
                baseline
            )
            /
            baseline
        )
        * 100.0;
}


string extract_first_bad_commit(
    const string& output
)
{
    static const regex pattern(
        R"(([0-9a-fA-F]{7,64})\s+is the first bad commit)"
    );

    smatch match;

    if (
        regex_search(
            output,
            match,
            pattern
        )
    ) {
        return match[1].str();
    }

    return {};
}


bool analyze_added_line(
    const string& line,
    string& evidence
)
{
    static const vector<string> keywords = {
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

    if (
        line.empty()
    ) {
        return false;
    }

    if (
        line[0] != '+'
    ) {
        return false;
    }

    // Ignore the diff header.
    if (
        line.rfind(
            "+++",
            0
        ) == 0
    ) {
        return false;
    }

    const string code =
        line.substr(1);

    for (
        const auto& keyword :
        keywords
    ) {
        if (
            code.find(
                keyword
            ) != string::npos
        ) {
            evidence = line;

            return true;
        }
    }

    return false;
}


// ============================================================
// JSON
// ============================================================

string json_escape(
    const string& value
)
{
    string escaped;

    escaped.reserve(value.size());

    for (
        const char character :
        value
    ) {
        switch (character) {

        case '"':
            escaped += "\\\"";
            break;

        case '\\':
            escaped += "\\\\";
            break;

        case '\n':
            escaped += "\\n";
            break;

        case '\r':
            escaped += "\\r";
            break;

        case '\t':
            escaped += "\\t";
            break;

        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}


bool json_get_string(
    const string& json,
    const string& key,
    string& value
)
{
    const regex pattern(
        "\"" +
        key +
        "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""
    );

    smatch match;

    if (
        !regex_search(
            json,
            match,
            pattern
        )
    ) {
        return false;
    }

    // The stored escapes are undone, so that a value survives the
    // round trip through json_escape unchanged.
    const string raw =
        match[1].str();

    value.clear();
    value.reserve(raw.size());

    for (
        size_t index = 0;
        index < raw.size();
        ++index
    ) {
        if (
            raw[index] != '\\' ||
            index + 1 >= raw.size()
        ) {
            value += raw[index];

            continue;
        }

        ++index;

        switch (raw[index]) {

        case 'n':
            value += '\n';
            break;

        case 'r':
            value += '\r';
            break;

        case 't':
            value += '\t';
            break;

        default:
            value += raw[index];
            break;
        }
    }

    return true;
}


bool json_get_number(
    const string& json,
    const string& key,
    double& value
)
{
    const regex pattern(
        "\"" +
        key +
        "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)"
    );

    smatch match;

    if (
        !regex_search(
            json,
            match,
            pattern
        )
    ) {
        return false;
    }

    try {
        value =
            stod(
                match[1].str()
            );
    }
    catch (...) {
        return false;
    }

    return true;
}


// ============================================================
// Environment metadata
// ============================================================

namespace {

// Read through RtlGetVersion rather than GetVersionExW, which reports
// the version of the manifest the process was built with rather than
// the one it is running on. It is also asked instead of running the
// "ver" command: that command is localised, and its output arrives in
// the console code page, which is not the encoding a JSON document is
// written in.
string windows_version()
{
    using RtlGetVersionFunction =
        LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

    const HMODULE ntdll =
        GetModuleHandleW(L"ntdll.dll");

    if (
        ntdll == nullptr
    ) {
        return "Windows";
    }

    const auto rtl_get_version =
        reinterpret_cast<RtlGetVersionFunction>(
            GetProcAddress(
                ntdll,
                "RtlGetVersion"
            )
        );

    if (
        rtl_get_version == nullptr
    ) {
        return "Windows";
    }

    RTL_OSVERSIONINFOW information{};

    information.dwOSVersionInfoSize =
        sizeof(information);

    if (
        rtl_get_version(&information) != 0
    ) {
        return "Windows";
    }

    return
        "Windows " +
        to_string(information.dwMajorVersion) +
        "." +
        to_string(information.dwMinorVersion) +
        "." +
        to_string(information.dwBuildNumber);
}


string environment_variable(
    const char* name
)
{
    const char* value =
        getenv(name);

    if (
        value == nullptr
    ) {
        return {};
    }

    return value;
}


string local_timestamp()
{
    const time_t now =
        time(nullptr);

    tm broken_down{};

    if (
        localtime_s(
            &broken_down,
            &now
        ) != 0
    ) {
        return "unknown";
    }

    char buffer[32];

    const size_t written =
        strftime(
            buffer,
            sizeof(buffer),
            "%Y-%m-%dT%H:%M:%S",
            &broken_down
        );

    if (
        written == 0
    ) {
        return "unknown";
    }

    return buffer;
}

}  // namespace


void fill_environment_metadata(
    Baseline& baseline
)
{
    baseline.os =
        windows_version();

    baseline.cpu =
        environment_variable(
            "PROCESSOR_IDENTIFIER"
        );

    // Read from the API rather than the environment variable, which
    // counts one processor group at most.
    const DWORD processors =
        GetActiveProcessorCount(
            ALL_PROCESSOR_GROUPS
        );

    baseline.cpu_count =
        processors > 0
            ? static_cast<int>(processors)
            : 0;

    baseline.compiler =
        PERFGUARD_COMPILER;

    baseline.build_type =
        PERFGUARD_BUILD_TYPE;

    baseline.cmake_generator =
        PERFGUARD_CMAKE_GENERATOR;

    baseline.timestamp =
        local_timestamp();

    baseline.perfguard_version =
        PERFGUARD_VERSION;
}


// ============================================================
// Baseline
// ============================================================

namespace {

fs::path baseline_file(
    const fs::path& project_root
)
{
    return
        project_root /
        ".perfguard" /
        "baseline.json";
}

}  // namespace


bool baseline_comparable(
    const Baseline& baseline,
    string& reason
)
{
    if (
        baseline.schema_version <
        Baseline::CURRENT_SCHEMA_VERSION
    ) {
        reason =
            "The stored baseline is version " +
            to_string(baseline.schema_version) +
            " and version " +
            to_string(Baseline::CURRENT_SCHEMA_VERSION) +
            " is current. It records no environment to "
            "compare against.";

        return false;
    }

    if (
        baseline.benchmark != BENCHMARK_NAME
    ) {
        reason =
            "The stored baseline measures \"" +
            baseline.benchmark +
            "\" and this build measures \"" +
            BENCHMARK_NAME +
            "\".";

        return false;
    }

    // A different compiler is a different toolchain for the same
    // source, and it is recorded for exactly that reason: the number
    // it produced is not this build's number to compare against.
    if (
        baseline.compiler != PERFGUARD_COMPILER
    ) {
        reason =
            "The stored baseline was built with \"" +
            baseline.compiler +
            "\" and this build uses \"" +
            PERFGUARD_COMPILER +
            "\".";

        return false;
    }

    if (
        baseline.build_type != PERFGUARD_BUILD_TYPE
    ) {
        reason =
            "The stored baseline was measured as \"" +
            baseline.build_type +
            "\" and this build is \"" +
            PERFGUARD_BUILD_TYPE +
            "\".";

        return false;
    }

    if (
        baseline.cmake_generator !=
        PERFGUARD_CMAKE_GENERATOR
    ) {
        reason =
            "The stored baseline was built with \"" +
            baseline.cmake_generator +
            "\" and this build uses \"" +
            PERFGUARD_CMAKE_GENERATOR +
            "\".";

        return false;
    }

    return true;
}


bool read_baseline(
    const fs::path& project_root,
    Baseline& baseline,
    string& error
)
{
    const fs::path file =
        baseline_file(project_root);

    if (
        !fs::exists(file)
    ) {
        error =
            "No baseline found. Run:\n"
            "  perfguard baseline";

        return false;
    }

    ifstream input(file);

    if (
        !input
    ) {
        error =
            "Could not open " +
            file.string() +
            ".";

        return false;
    }

    stringstream buffer;

    buffer << input.rdbuf();

    const string content =
        trim(buffer.str());

    if (
        content.empty()
    ) {
        error =
            "The baseline file is empty.";

        return false;
    }

    // A version 1 baseline stores only an average, so the fields a
    // version 2 comparison needs are absent rather than unreadable
    // and the caller is told to rebuild it.
    double schema_version = 1.0;

    json_get_number(
        content,
        "schema_version",
        schema_version
    );

    baseline =
        Baseline{};

    baseline.schema_version =
        static_cast<int>(schema_version);

    json_get_string(
        content,
        "benchmark",
        baseline.benchmark
    );

    if (
        !json_get_string(
            content,
            "commit",
            baseline.commit
        ) ||
        baseline.commit.empty()
    ) {
        error =
            "The baseline has no commit.";

        return false;
    }

    double average = 0.0;

    if (
        !json_get_number(
            content,
            "average_us",
            average
        ) ||
        average <= 0.0
    ) {
        error =
            "The baseline has no usable average.";

        return false;
    }

    baseline.average_us =
        average;

    // Absent from a version 1 baseline; the caller decides whether
    // that matters.
    json_get_number(content, "median_us", baseline.median_us);
    json_get_number(content, "stdev_us", baseline.stdev_us);
    json_get_number(content, "min_us", baseline.min_us);
    json_get_number(content, "max_us", baseline.max_us);

    // Every verdict is the change of this median, so a version that
    // records one is refused without it rather than compared as a
    // zero baseline: calculate_change reads a baseline of zero as no
    // change at all, which would report a green verdict from a file
    // that could not supply the figure the verdict is drawn from.
    if (
        baseline.schema_version > 1 &&
        baseline.median_us <= 0.0
    ) {
        error =
            "The baseline has no usable median.";

        return false;
    }

    double runs = 0.0;

    if (
        json_get_number(
            content,
            "runs",
            runs
        )
    ) {
        baseline.runs =
            static_cast<int>(runs);
    }

    double warmup_runs = 0.0;

    if (
        json_get_number(
            content,
            "warmup_runs",
            warmup_runs
        )
    ) {
        baseline.warmup_runs =
            static_cast<int>(warmup_runs);
    }

    json_get_string(content, "os", baseline.os);
    json_get_string(content, "cpu", baseline.cpu);
    json_get_string(
        content,
        "compiler",
        baseline.compiler
    );
    json_get_string(
        content,
        "build_type",
        baseline.build_type
    );
    json_get_string(
        content,
        "cmake_generator",
        baseline.cmake_generator
    );
    json_get_string(
        content,
        "timestamp",
        baseline.timestamp
    );
    json_get_string(
        content,
        "perfguard_version",
        baseline.perfguard_version
    );

    double cpu_count = 0.0;

    if (
        json_get_number(
            content,
            "cpu_count",
            cpu_count
        )
    ) {
        baseline.cpu_count =
            static_cast<int>(cpu_count);
    }

    return true;
}


bool write_baseline(
    const fs::path& project_root,
    const Baseline& baseline,
    string& error
)
{
    const fs::path file =
        baseline_file(project_root);

    error_code code;

    fs::create_directories(
        file.parent_path(),
        code
    );

    if (
        code
    ) {
        error =
            "Could not create " +
            file.parent_path().string() +
            ".";

        return false;
    }

    ofstream output(file);

    if (
        !output
    ) {
        error =
            "Could not create " +
            file.string() +
            ".";

        return false;
    }

    output
        << fixed
        << setprecision(2);

    output
        << "{\n"
        << "    \"schema_version\": "
        << baseline.schema_version
        << ",\n"
        << "    \"benchmark\": \""
        << json_escape(baseline.benchmark)
        << "\",\n"
        << "    \"commit\": \""
        << json_escape(baseline.commit)
        << "\",\n"
        << "    \"average_us\": "
        << baseline.average_us
        << ",\n"
        << "    \"median_us\": "
        << baseline.median_us
        << ",\n"
        << "    \"stdev_us\": "
        << baseline.stdev_us
        << ",\n"
        << "    \"min_us\": "
        << baseline.min_us
        << ",\n"
        << "    \"max_us\": "
        << baseline.max_us
        << ",\n"
        << "    \"runs\": "
        << baseline.runs
        << ",\n"
        << "    \"warmup_runs\": "
        << baseline.warmup_runs
        << ",\n"
        << "    \"metadata\": {\n"
        << "        \"os\": \""
        << json_escape(baseline.os)
        << "\",\n"
        << "        \"cpu\": \""
        << json_escape(baseline.cpu)
        << "\",\n"
        << "        \"cpu_count\": "
        << baseline.cpu_count
        << ",\n"
        << "        \"compiler\": \""
        << json_escape(baseline.compiler)
        << "\",\n"
        << "        \"build_type\": \""
        << json_escape(baseline.build_type)
        << "\",\n"
        << "        \"cmake_generator\": \""
        << json_escape(baseline.cmake_generator)
        << "\",\n"
        << "        \"timestamp\": \""
        << json_escape(baseline.timestamp)
        << "\",\n"
        << "        \"perfguard_version\": \""
        << json_escape(baseline.perfguard_version)
        << "\"\n"
        << "    }\n"
        << "}\n";

    output.close();

    if (
        !output
    ) {
        error =
            "Could not write " +
            file.string() +
            ".";

        return false;
    }

    return true;
}

}  // namespace perfguard
