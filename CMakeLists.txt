cmake_minimum_required(VERSION 3.20)
project(PerfGuard
        VERSION 0.1.0
        LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Recorded in the baseline and checked before a stored measurement is
# compared against a fresh one. Everything that can change a compiled
# number is named here, because a mismatch in any of it is a reason to
# distrust the comparison rather than to report it.
#
# The build type comes from the $<CONFIG> expression rather than
# CMAKE_BUILD_TYPE, which is empty under the multi-config Visual Studio
# generator this project uses.
function(perfguard_add_metadata_definitions target)
    target_compile_definitions(${target} PRIVATE
        PERFGUARD_VERSION="${PROJECT_VERSION}"
        PERFGUARD_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        PERFGUARD_BUILD_TYPE="$<CONFIG>"
        PERFGUARD_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    )
endfunction()

add_executable(perfguard
    src/main.cpp
    src/evidence.cpp
    src/diagnosis.cpp
    src/bisect.cpp
    src/core.cpp
    src/measurement.cpp
)
target_include_directories(perfguard PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
perfguard_add_metadata_definitions(perfguard)
add_executable(sort_1m
    benchmarks/sort_1m.cpp
)
target_include_directories(sort_1m PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

enable_testing()
add_executable(perfguard_tests
    tests/perfguard_tests.cpp
    src/core.cpp
    src/measurement.cpp
)
target_include_directories(perfguard_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
perfguard_add_metadata_definitions(perfguard_tests)
add_test(NAME perfguard_tests COMMAND perfguard_tests)
