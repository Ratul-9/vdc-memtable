#!/usr/bin/env bash
# Build everything and run the full single-threaded test and benchmark suite (Linux).
#
#   scripts/run_bench.sh [--quick] [--cpu N] [--reps R] [--skip-hugetlb]
#
#   --quick          fewer key counts (1K,100K,1M) and 3 repetitions
#   --cpu N          pin to CPU N with taskset (default 2; pick an idle core, a P-core on hybrid CPUs)
#   --reps R         repetitions per cell and counter set (default 5)
#   --skip-hugetlb   run base pages only (otherwise a missing hugetlb pool is an error)
#
# Output: results/<timestamp>/ with markdown reports (tables) and CSV files.
# Before the first run:  sudo scripts/setup_hugepages.sh   and   scripts/perf_counters.sh
set -uo pipefail
cd "$(dirname "$0")/.."

QUICK=0
CPU=2
REPS=5
SKIP_HUGE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick) QUICK=1 ;;
        --cpu) CPU="$2"; shift ;;
        --reps) REPS="$2"; shift ;;
        --skip-hugetlb) SKIP_HUGE=1 ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "ERROR: the benchmarks need Linux (hardware counters, hugetlb pages)." >&2
    exit 1
fi

KEYS="1K,10K,100K,500K,1M"
JS="6,8,11,15,20"
if (( QUICK )); then
    KEYS="1K,100K,1M"
    REPS=3
fi

PAGES="base,hugetlb"
if (( SKIP_HUGE )); then
    PAGES="base"
else
    FREE="$(awk '/HugePages_Free/ {print $2}' /proc/meminfo)"
    if (( FREE < 64 )); then
        echo "ERROR: the hugetlb pool has $FREE free 2 MiB pages; at least 64 are needed." >&2
        echo "       Run: sudo scripts/setup_hugepages.sh    (or pass --skip-hugetlb)" >&2
        exit 1
    fi
fi

PARANOID="$(cat /proc/sys/kernel/perf_event_paranoid)"
if (( PARANOID > 2 )); then
    echo "WARNING: perf_event_paranoid=$PARANOID blocks hardware counters; reports will have timings only."
    echo "         Run: sudo scripts/perf_counters.sh --enable"
fi

PIN=()
if command -v taskset >/dev/null 2>&1; then
    PIN=(taskset -c "$CPU")
fi
run() { ${PIN[@]+"${PIN[@]}"} "$@"; }

OUT="results/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
FAIL=0

echo "== build"
make clean >/dev/null
if ! make all > "$OUT/build.log" 2>&1; then
    echo "ERROR: build failed, see $OUT/build.log" >&2
    exit 1
fi

echo "== unit tests"
if ! ./build/test_runner | tee "$OUT/unit_tests.txt"; then
    echo "ERROR: unit tests failed" >&2
    FAIL=1
fi

echo "== 1/5 main matrix: vdc, j = $JS, keys = $KEYS, pages = $PAGES"
run ./build/vdc_bench --impl vdc --j "$JS" --keys "$KEYS" --pages "$PAGES" --reps "$REPS" \
    --csv "$OUT/results_vdc.csv" > "$OUT/report_vdc.md" || FAIL=1

echo "== 2/5 baselines: vdc (j = 6, 11, 20) vs hash table vs skip list"
run ./build/vdc_bench --impl vdc,hash,skiplist --j 6,11,20 --keys "$KEYS" --pages "$PAGES" --reps "$REPS" \
    --ops insert,get_hit,get_miss,update,delete --csv "$OUT/results_baselines.csv" \
    > "$OUT/report_baselines.md" || FAIL=1

echo "== 3/5 analytic experiments (bijection, cache-set aliasing, layout)"
./build/exp_01_bijection_check > "$OUT/exp01_bijection.md" || FAIL=1
./build/exp_02_partial_reversal_j_sweep > "$OUT/exp02_aliasing.md" || FAIL=1
./build/exp_05_slot_layout > "$OUT/exp05_layout.md" || FAIL=1

echo "== 4/5 prefetch distance"
run ./build/exp_04_prefetch_batching --pages "$PAGES" > "$OUT/exp04_prefetch.md" || FAIL=1

echo "== 5/5 create / fill / clear cost"
run ./build/exp_06_fill_and_rotation --pages "$PAGES" > "$OUT/exp06_fill.md" || FAIL=1

echo
echo "Results in $OUT:"
ls -1 "$OUT"
if (( FAIL )); then
    echo "WARNING: at least one step failed or reported a failed check; see the files above." >&2
    exit 1
fi
echo "All steps finished. Start with the Checks table in $OUT/report_vdc.md"
