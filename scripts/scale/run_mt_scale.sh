#!/usr/bin/env bash
# Experiment 6: threads at every key count (1M - 100M keys), Linux, under the memory budget.
#
#   scripts/scale/run_mt_scale.sh [--quick] [--pages hugetlb|base] [--budget-gib 9] [--max-keys 100M]
#                                 [--threads 1,2,4,6,8,12] [--cpus LIST]
#
#   --quick         keys 1M,10M,25M; threads 1,2,4,6
#   --pages         hugetlb (default: reserves a 2 MiB pool with sudo and gives it back) or base
#   --cpus LIST     CPU of thread 0, 1, ... Default: one hyper-thread per physical core first, then siblings
#
# Output: results/mtscale-<timestamp>/ with report_mtscale.md, results_mtscale.csv, logs.
set -uo pipefail
cd "$(dirname "$0")/../.."

QUICK=0
PAGES=hugetlb
BUDGET=9
MAXKEYS=""
THREADS="1,2,4,6,8,12"
CPUS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick) QUICK=1 ;;
        --pages) PAGES="$2"; shift ;;
        --budget-gib) BUDGET="$2"; shift ;;
        --max-keys) MAXKEYS="$2"; shift ;;
        --threads) THREADS="$2"; shift ;;
        --cpus) CPUS="$2"; shift ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "ERROR: run this on Linux." >&2
    exit 1
fi
case "$PAGES" in hugetlb|base) ;; *) echo "ERROR: --pages must be hugetlb or base" >&2; exit 2 ;; esac
if awk -v b="$BUDGET" 'BEGIN { exit !(b > 10 || b <= 0) }'; then
    echo "ERROR: --budget-gib must be between 0 and 10." >&2
    exit 1
fi

to_num() { awk -v s="$1" 'BEGIN { m = 1; if (s ~ /[Mm]$/) m = 1000000; else if (s ~ /[Kk]$/) m = 1000; sub(/[KkMm]$/, "", s); printf "%d", s * m }'; }

KEYS="1M,10M,25M,50M,75M,100M"
if (( QUICK )); then
    KEYS="1M,10M,25M"
    THREADS="1,2,4,6"
fi
if [[ -n "$MAXKEYS" ]]; then
    MAX="$(to_num "$MAXKEYS")"; OUTK=""
    IFS=',' read -ra arr <<< "$KEYS"
    for k in "${arr[@]}"; do (( $(to_num "$k") <= MAX )) && OUTK="${OUTK:+$OUTK,}$k"; done
    KEYS="$OUTK"
fi
if [[ -z "$KEYS" ]]; then echo "ERROR: no key counts left" >&2; exit 2; fi

# One hyper-thread per physical core first, then the siblings.
if [[ -z "$CPUS" ]]; then
    CPUS="$(for c in /sys/devices/system/cpu/cpu[0-9]*; do
                n="${c##*cpu}"
                core="$(cat "$c/topology/core_id" 2>/dev/null || echo "$n")"
                pkg="$(cat "$c/topology/physical_package_id" 2>/dev/null || echo 0)"
                echo "$pkg $core $n"
            done | sort -n -k1,1 -k2,2 -k3,3 | awk '{ seen[$1" "$2]++; print seen[$1" "$2], $3 }' \
                 | sort -n -k1,1 -s | awk '{print $2}' | paste -sd, -)"
fi

OUT="results/mtscale-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
mem_field() { awk -v f="$1:" '$1 == f { print $2 }' /proc/meminfo; }
echo "Memory: total $(( $(mem_field MemTotal) / 1048576 )) GiB, available $(( $(mem_field MemAvailable) / 1048576 )) GiB, budget ${BUDGET} GiB"

echo "== build"
if ! make all > "$OUT/build.log" 2>&1; then
    echo "ERROR: build failed, see $OUT/build.log" >&2
    exit 1
fi
echo "== unit tests"
./build/test_runner | tee "$OUT/unit_tests.txt" || { echo "ERROR: unit tests failed" >&2; exit 1; }

ARGS=(--keys "$KEYS" --threads "$THREADS" --pages "$PAGES" --budget-gib "$BUDGET" --cpus "$CPUS")
./build/dat_mtscale "${ARGS[@]}" --plan > "$OUT/plan.md" 2>&1

if [[ "$PAGES" == hugetlb ]]; then
    # Largest structure: ceil(maxN / 32768) huge pages of nodes + the row table + margin.
    MAXN=0
    IFS=',' read -ra arr <<< "$KEYS"
    for k in "${arr[@]}"; do v="$(to_num "$k")"; (( v > MAXN )) && MAXN=$v; done
    NEED=$(( (MAXN + 32767) / 32768 + 6 ))
    PREV="$(cat /proc/sys/vm/nr_hugepages)"
    echo "== reserving $NEED huge pages ($(( NEED * 2 )) MiB) (sudo)"
    trap 'sudo sh -c "echo $PREV > /proc/sys/vm/nr_hugepages"; echo "== huge-page pool restored to $PREV pages"' EXIT
    sudo sh -c "sync; echo 3 > /proc/sys/vm/drop_caches; echo 1 > /proc/sys/vm/compact_memory 2>/dev/null; echo $NEED > /proc/sys/vm/nr_hugepages"
    GOT="$(cat /proc/sys/vm/nr_hugepages)"
    if (( GOT < NEED )); then
        echo "NOTE: only $GOT of $NEED huge pages reserved (fragmented memory); the largest sizes will be skipped."
        echo "      Reboot and run again, or use --pages base."
    fi
else
    HT="$(mem_field HugePages_Total)"
    if (( HT > 0 )); then
        echo "== releasing $HT reserved huge pages so normal pages can use that memory (sudo)"
        sudo sh -c 'echo 0 > /proc/sys/vm/nr_hugepages' || { echo "ERROR: could not release the pool" >&2; exit 1; }
    fi
fi

echo "== experiment 6: keys $KEYS, threads $THREADS, $PAGES pages, CPU order $CPUS  [$(date +%H:%M:%S)]"
./build/dat_mtscale "${ARGS[@]}" --csv "$OUT/results_mtscale.csv" > "$OUT/report_mtscale.md" 2> "$OUT/progress.log"
RC=$?
echo "== finished [$(date +%H:%M:%S)]"
grep -E "Memory budget" "$OUT/report_mtscale.md" | head -1 | sed 's/^/   /'

echo
echo "Results in $OUT:"
ls -1 "$OUT"
if (( RC != 0 )); then
    echo "WARNING: dat_mtscale exited with $RC (wrong results or an error); see the Checks table and progress.log" >&2
    exit 1
fi
echo "Start with the Checks and \"Throughput vs key count\" tables in $OUT/report_mtscale.md"
