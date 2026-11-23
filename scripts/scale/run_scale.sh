#!/usr/bin/env bash
# Scale experiments 1-5 (1M - 100M keys) on Linux, never above the memory budget.
#
#   scripts/scale/run_scale.sh [--quick] [--budget-gib 9] [--phase base|huge|all] [--cpu 2]
#                              [--max-keys 100M] [--skiplist-max 10M] [--reps 3]
#
#   --quick          smaller sizes (1M,10M,25M; density at 10M; huge pages at 10M) and 2 repetitions
#   --budget-gib X   program + structure memory limit (default 9; 10 is the absolute maximum)
#   --phase          base = experiments 1, 2, 3, 5 on normal pages
#                    huge = experiment 4 on 2 MiB pages (reserves a pool with sudo, frees it after)
#                    all  = base, then huge (default)
#   --max-keys N     drop key counts above N (e.g. 50M if the machine is busy)
#   --skiplist-max N largest skip list (default 10M; it is slow)
#
# Only one experiment process runs at a time, and inside it only one structure
# exists at a time. Output: results/scale-<timestamp>/ (one report per experiment,
# CSVs, logs, summary.md).
set -uo pipefail
cd "$(dirname "$0")/../.."

QUICK=0
BUDGET=9
PHASE=all
CPU=2
MAXKEYS=""
SKIPMAX=10M
REPS=3
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick) QUICK=1 ;;
        --budget-gib) BUDGET="$2"; shift ;;
        --phase) PHASE="$2"; shift ;;
        --cpu) CPU="$2"; shift ;;
        --max-keys) MAXKEYS="$2"; shift ;;
        --skiplist-max) SKIPMAX="$2"; shift ;;
        --reps) REPS="$2"; shift ;;
        -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "ERROR: run this on Linux." >&2
    exit 1
fi
if awk -v b="$BUDGET" 'BEGIN { exit !(b > 10 || b <= 0) }'; then
    echo "ERROR: --budget-gib must be between 0 and 10." >&2
    exit 1
fi
case "$PHASE" in base|huge|all) ;; *) echo "ERROR: --phase must be base, huge or all" >&2; exit 2 ;; esac

# "10M" -> 10000000
to_num() { awk -v s="$1" 'BEGIN { m = 1; if (s ~ /[Mm]$/) m = 1000000; else if (s ~ /[Kk]$/) m = 1000; sub(/[KkMm]$/, "", s); printf "%d", s * m }'; }
# Keep only the key counts <= MAXKEYS.
filter_keys() {
    local list="$1" out="" k
    if [[ -z "$MAXKEYS" ]]; then echo "$list"; return; fi
    local max; max="$(to_num "$MAXKEYS")"
    IFS=',' read -ra arr <<< "$list"
    for k in "${arr[@]}"; do
        if (( $(to_num "$k") <= max )); then out="${out:+$out,}$k"; fi
    done
    echo "$out"
}

if (( QUICK )); then
    KEYS_MAIN="1M,10M,25M"; KEYS_DENSITY="10M"; KEYS_HUGE="10M"; REPS=2
else
    KEYS_MAIN="1M,10M,25M,50M,75M,100M"; KEYS_DENSITY="10M,100M"; KEYS_HUGE="10M,50M,100M"
fi
KEYS_MAIN="$(filter_keys "$KEYS_MAIN")"
KEYS_DENSITY="$(filter_keys "$KEYS_DENSITY")"
KEYS_HUGE="$(filter_keys "$KEYS_HUGE")"

OUT="results/scale-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
FAIL=0

mem_field() { awk -v f="$1:" '$1 == f { print $2 }' /proc/meminfo; }
echo "Memory: total $(( $(mem_field MemTotal) / 1048576 )) GiB, available $(( $(mem_field MemAvailable) / 1048576 )) GiB, budget ${BUDGET} GiB"
if (( $(mem_field MemAvailable) < $(awk -v b="$BUDGET" 'BEGIN { printf "%d", (b + 1) * 1048576 }') )); then
    echo "NOTE: less than budget + 1 GiB is available; dat_scale lowers its budget automatically."
fi

echo "== build"
if ! make all > "$OUT/build.log" 2>&1; then
    echo "ERROR: build failed, see $OUT/build.log" >&2
    exit 1
fi
echo "== unit tests"
./build/test_runner | tee "$OUT/unit_tests.txt" || { echo "ERROR: unit tests failed" >&2; exit 1; }

PIN=()
if command -v taskset >/dev/null 2>&1; then PIN=(taskset -c "$CPU"); fi

run_exp() {
    local e="$1" name="$2" keys="$3"; shift 3
    if [[ -z "$keys" ]]; then
        echo "== experiment $e ($name): no key counts left after --max-keys, skipped"
        return
    fi
    echo "== experiment $e ($name), keys $keys  [$(date +%H:%M:%S)]"
    ${PIN[@]+"${PIN[@]}"} ./build/dat_scale --experiment "$e" --keys "$keys" --reps "$REPS" --budget-gib "$BUDGET" \
        --csv "$OUT/results_e$e.csv" "$@" > "$OUT/e${e}_${name}.md" 2> "$OUT/e${e}_${name}.log"
    local rc=$?
    if (( rc != 0 )); then
        echo "WARNING: experiment $e exited with $rc (see $OUT/e${e}_${name}.log)" >&2
        FAIL=1
    fi
    grep -E "Peak process memory" "$OUT/e${e}_${name}.md" | head -1 | sed 's/^/   /'
}

# ---------------- base-page phase ----------------
if [[ "$PHASE" == base || "$PHASE" == all ]]; then
    HT="$(mem_field HugePages_Total)"
    if (( HT > 0 )); then
        echo "== releasing $HT reserved huge pages ($(( HT * 2 )) MiB) so normal-page experiments can use that memory (sudo)"
        if ! sudo sh -c 'echo 0 > /proc/sys/vm/nr_hugepages'; then
            echo "ERROR: could not release the huge-page pool." >&2
            exit 1
        fi
    fi
    run_exp 1 flat_scale "$KEYS_MAIN"
    run_exp 2 row_cost "$KEYS_MAIN"
    run_exp 3 density "$KEYS_DENSITY"
    run_exp 5 baselines "$KEYS_MAIN" --skiplist-max "$SKIPMAX"
fi

# ---------------- huge-page phase ----------------
if [[ "$PHASE" == huge || "$PHASE" == all ]] && [[ -n "$KEYS_HUGE" ]]; then
    NEED="$(./build/dat_scale --experiment 4 --keys "$KEYS_HUGE" --budget-gib "$BUDGET" --hugepages-needed 2>/dev/null)"
    NEED=$(( ${NEED:-0} + 4 ))
    PREV="$(cat /proc/sys/vm/nr_hugepages)"
    echo "== reserving $NEED huge pages ($(( NEED * 2 )) MiB) for experiment 4 (sudo)"
    # Always give the memory back, even if the script is interrupted.
    trap 'sudo sh -c "echo $PREV > /proc/sys/vm/nr_hugepages"; echo "== huge-page pool restored to $PREV pages"' EXIT
    sudo sh -c "sync; echo 3 > /proc/sys/vm/drop_caches; echo 1 > /proc/sys/vm/compact_memory 2>/dev/null; echo $NEED > /proc/sys/vm/nr_hugepages"
    GOT="$(cat /proc/sys/vm/nr_hugepages)"
    if (( GOT < NEED )); then
        echo "NOTE: only $GOT of $NEED huge pages could be reserved (fragmented memory)."
        echo "      Experiment 4 will skip the sizes that do not fit; reboot and re-run --phase huge for all of them."
    fi
    run_exp 4 hugepages "$KEYS_HUGE"
fi

echo "== summary"
python3 scripts/scale/summarize_scale.py "$OUT" > "$OUT/summary.md" 2> "$OUT/summary.log" || echo "WARNING: summary failed (see $OUT/summary.log)"

echo
echo "Results in $OUT:"
ls -1 "$OUT"
if (( FAIL )); then
    echo "WARNING: at least one experiment failed or reported wrong results." >&2
    exit 1
fi
echo "Start with $OUT/summary.md"
