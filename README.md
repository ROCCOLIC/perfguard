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

Run the tests with `./build/Debug/perfguard_tests.exe`, or
`ctest --test-dir build -C Debug`. The CLI part measures the benchmark,
so the suite takes some seconds rather than none.

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
| `--report` | Compare the stored baseline against a fresh measurement, write `perfguard-report.md` into the project root |
| `--analyze` | Compare the working tree against the stored baseline, then bisect, diagnose and report |
| `--bisect` | Find the first bad commit |
| `--diagnose <commit>` | Build one commit and compare it |
| `--evidence <commit>` | Show what a commit changed |
| `--simulate-regression` | Self-test: inject a slowdown and look for it |
| `--help`, `--version` | Usage and version |

| Option | Meaning |
|---|---|
| `--threshold <percent>` | Regression threshold (default 10) |
| `--runs <n>` | Samples to take (default 5, or 9 per side for `--check`) |
| `--json` | Machine-readable output on stdout |
| `--force` | `--bisect` / `--analyze`: discard a bisect session already in progress |

`--json` is supported by every command except the internal
`--bisect-test` step. Human narration moves to stderr, so stdout carries
the document alone.

A command line the tool cannot parse is reported the same way, and for
the same reason: a typo in a CI job is a case where the caller is
reading stdout as a document, so a bad option, a bad command, a missing
argument or a surplus one produces `{"status": "error"}` there and the
usage text goes to stderr with the reason. `--json` is honoured wherever
it is written on the line, including after the word that went wrong.

A command name the tool does not know is one of those errors rather than
a request for the default benchmark: `perfguard chek` reports the typo
instead of measuring and handing back a verdict. A command takes a fixed
number of words for the same reason, so `perfguard --evidence 41f6091
HEAD` refuses the word it has no use for rather than dropping it in
silence.

An option value has to be a whole, finite number. `--threshold 10x` and
`--runs 3.7` are refused rather than read as 10 and as 3, and
`--threshold nan` is refused rather than read as a threshold that fails
every comparison and reports no regression.

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

The verdict line carries that spread, so a change can be read against
the movement that decided it rather than against the threshold alone:

    Result: REGRESSION (noise ±1.83%)

The figure is twice the larger relative standard deviation of the
samples behind the two medians, in the same percent as the change and
the threshold. Twice, because the change is the difference of two
independently noisy medians, so both sides contribute to it. A change
within a noise that reads as a large fraction of the threshold is one
to measure again before acting on.

The JSON documents carry the spread behind both medians, as
`baseline_stats` and `current_stats`, and the figure itself as
`noise_percent`, so a CI job reads the number rather than working it out
from the samples. It is left out when the samples are too few to draw it
from. `--check` also keeps the nested `stats.baseline` /
`stats.current` it wrote before the pair existed.

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
