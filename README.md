# PerfGuard

Detects performance regressions by building and measuring the same
benchmark in one session, on one machine, and comparing medians.

## Build

    cmake -S . -B build -G "Visual Studio 17 2022"
    cmake --build build --config Debug

| Target | What it is |
|---|---|
| `perfguard` | the tool |
| `sort_1m` | the benchmark it measures |
| `perfguard_tests` | tests for the parsing, statistics and baseline code |

Run the tests with `./build/Debug/perfguard_tests.exe`, or `ctest --test-dir build -C Debug`.

## First run: record a baseline

PerfGuard compares a measurement against a stored one, so record one
from the commit you want to treat as good:

    perfguard baseline

This measures the working tree and writes `.perfguard/baseline.json`,
recording the commit, the numbers, and the environment that produced
them. `.perfguard/` is not version-controlled: a measurement only means
something next to the machine and build configuration that produced it,
so every checkout records its own.

The tree has to be clean. What is measured is the benchmark built out
of the working tree, while what is recorded is HEAD, and `git bisect`
later treats the recorded commit as good and searches from it. A tree
with uncommitted changes would leave that search anchored to code that
was never measured, so `baseline` refuses until the work is committed
or stashed.

## Commands

| Command | What it does |
|---|---|
| `baseline` | Measure the working tree and store it |
| `--check` | Build and measure the baseline commit and HEAD, then compare |
| `--report` | Compare the stored baseline against a fresh measurement, write a Markdown report |
| `--analyze` | Compare the working tree against the stored baseline, then bisect, diagnose and report |
| `--bisect` | Find the first bad commit |
| `--diagnose <commit>` | Build one commit and compare it |
| `--evidence <commit>` | Show what a commit changed |
| `--simulate-regression` | Self-test: inject a slowdown and look for it |
| `--help`, `--version` | Usage and version |

| Option | Meaning |
|---|---|
| `--threshold <percent>` | Regression threshold (default 10) |
| `--runs <n>` | Samples per side (default 9 for `--check`) |
| `--json` | Machine-readable output on stdout |
| `--force` | `--bisect` / `--analyze`: discard a bisect session already in progress |

`--json` is supported by every command except the internal
`--bisect-test` step. Human narration moves to stderr, so stdout carries
the document alone.

Because `--check` builds both sides, it takes minutes, not seconds.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | No regression |
| 1 | Regression detected |
| 2 | Error, or the stored baseline cannot be compared |

`--bisect-test` is an internal step of `git bisect` and keeps its own
contract: 0 = good, 1 = bad, 125 = skip.

## Threshold and noise

The threshold defaults to 10%. The same source measured in two build
directories on one machine has been seen a few percent apart, which is
close enough to that threshold to decide a verdict on noise alone. Two
things follow.

`--check` builds both sides in the same session and measures them
alternately, A, B, A, B, so drift between the two builds cancels out
instead of being read as a regression.

A baseline is only compared when its environment matches. An older
`schema_version`, a different benchmark, a different `build_type` or a
different CMake generator is refused with exit code 2 rather than
compared, with a pointer to re-run `perfguard baseline`.

The verdict is drawn from the median of several samples rather than one
run; `--runs` sets how many. Outliers are only rejected when there are
at least seven samples, and at most one is dropped, so a small sample is
reported as measured rather than trimmed into a shape it does not have.

## CI

`--json` on stdout makes the verdict readable without parsing prose:

    perfguard --check --json > perfguard.json
    status=$?
    case $status in
      0) echo "no regression" ;;
      1) echo "regression" ;;
      2) echo "could not compare the baseline" ;;
    esac

`tools/bisect_check.ps1` and `tools/diagnose_commit.ps1` are thin
wrappers: they build the tool, call the CLI, and pass its exit code on.
