#!/usr/bin/env bash
# Build and run the multithreaded benchmark (Linux).
#
#   scripts/run_mt_bench.sh [--quick] [--pages base|hugetlb] [--cpus LIST] [--threads LIST]
#
#   --quick          threads 1,2,4,6; j 11,20; uniform + interleaved; 200K ops; 1 repetition
#   --pages MODE     base or hugetlb (default hugetlb; needs sudo scripts/setup_hugepages.sh)
#   --cpus LIST      CPU for thread 0, 1, 2, ... (consumers get the first ones).
#                    Default: one hyper-thread per physical core first, then the siblings.
#   --threads LIST   total threads per run (default 1,2,4,6,8,12)
#
# Output: results/mt-<timestamp>/ with report_mt.md (tables), results_mt.csv, unit_tests.txt.
set -uo pipefail
cd "$(dirname "$0")/.."

QUICK=0
PAGES=hugetlb
CPUS=""
THREADS="1,2,4,6,8,12"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick) QUICK=1 ;;
        --pages) PAGES="$2"; shift ;;
        --cpus) CPUS="$2"; shift ;;
        --threads) THREADS="$2"; shift ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "ERROR: run this on Linux (thread pinning, hugetlb pages)." >&2
    exit 1
fi

if [[ "$PAGES" == "hugetlb" ]]; then
    FREE="$(awk '/HugePages_Free/ {print $2}' /proc/meminfo)"
    if (( FREE < 32 )); then
        echo "ERROR: the hugetlb pool has $FREE free 2 MiB pages; 32 are needed." >&2
        echo "       Run: sudo scripts/setup_hugepages.sh    (or pass --pages base)" >&2
        exit 1
    fi
fi

# Default CPU order: the first hyper-thread of every physical core, then the second ones,
# so that up to <cores> threads never share a core.
if [[ -z "$CPUS" ]]; then
    CPUS="$(for c in /sys/devices/system/cpu/cpu[0-9]*; do
                n="${c##*cpu}"
                core="$(cat "$c/topology/core_id" 2>/dev/null || echo "$n")"
                pkg="$(cat "$c/topology/physical_package_id" 2>/dev/null || echo 0)"
                echo "$pkg $core $n"
            done | sort -n -k1,1 -k2,2 -k3,3 | awk '{ seen[$1" "$2]++; print seen[$1" "$2], $3 }' \
                 | sort -n -k1,1 -s | awk '{print $2}' | paste -sd, -)"
fi
CORES="$(lscpu -p=CORE 2>/dev/null | grep -v '^#' | sort -u | wc -l)"

ARGS=(--threads "$THREADS" --pages "$PAGES" --cpus "$CPUS")
if (( QUICK )); then
    ARGS=(--threads "1,2,4,6" --j 11,20 --dists uniform,interleaved --ops 200K --reps 1 --pages "$PAGES" --cpus "$CPUS")
fi

OUT="results/mt-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

echo "== build"
if ! make all > "$OUT/build.log" 2>&1; then
    echo "ERROR: build failed, see $OUT/build.log" >&2
    exit 1
fi

echo "== unit tests"
./build/test_runner | tee "$OUT/unit_tests.txt" || { echo "ERROR: unit tests failed" >&2; exit 1; }

echo "== multithreaded benchmark (physical cores: ${CORES:-?}, CPU order: $CPUS)"
echo "   threads above the physical core count share cores (hyper-threads); the report says so."
./build/vdc_mtbench "${ARGS[@]}" --csv "$OUT/results_mt.csv" > "$OUT/report_mt.md" 2> "$OUT/progress.log"
RC=$?

echo
echo "Results in $OUT:"
ls -1 "$OUT"
if (( RC != 0 )); then
    echo "WARNING: vdc_mtbench reported wrong results or failed (exit $RC); see the Checks table and progress.log" >&2
    exit 1
fi
echo "Start with the Checks table in $OUT/report_mt.md"
