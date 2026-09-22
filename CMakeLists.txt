cmake_minimum_required(VERSION 3.20)
project(PerfGuard
        VERSION 0.1.0
        LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(perfguard
    src/main.cpp
    src/evidence.cpp
    src/diagnosis.cpp
    src/bisect.cpp
)
add_executable(sort_1m
    benchmarks/sort_1m.cpp
)