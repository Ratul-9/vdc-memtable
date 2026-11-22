#!/usr/bin/env bash
# Check (and optionally allow) hardware performance counters for vdc_bench.
#
#   scripts/perf_counters.sh            show the current state
#   sudo scripts/perf_counters.sh --enable   set kernel.perf_event_paranoid=1 (until reboot)
#
# vdc_bench counts user-space events of its own process (exclude_kernel), which
# the kernel allows for perf_event_paranoid <= 2. Some distributions default to
# 3 or 4, which blocks it. Virtual machines often expose no hardware counters at
# all; use bare metal for the cache / TLB numbers.
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "Hardware counters through perf_event_open are Linux only; vdc_bench reports timings elsewhere."
    exit 0
fi

PARANOID="$(cat /proc/sys/kernel/perf_event_paranoid)"
echo "kernel.perf_event_paranoid = $PARANOID (vdc_bench needs <= 2)"

if [[ "${1:-}" == "--enable" ]]; then
    if [[ $EUID -ne 0 ]]; then
        echo "ERROR: run with sudo to change perf_event_paranoid." >&2
        exit 1
    fi
    sysctl -w kernel.perf_event_paranoid=1
    PARANOID=1
fi

if (( PARANOID > 2 )); then
    echo "WARNING: counters are blocked. Run: sudo $0 --enable"
fi

if grep -qE '^flags.* hypervisor' /proc/cpuinfo 2>/dev/null; then
    echo "NOTE: running under a hypervisor; hardware counters may be missing or incomplete."
fi

if command -v perf >/dev/null 2>&1; then
    echo "Test with perf (the same events vdc_bench opens):"
    perf stat -e cycles,instructions,L1-dcache-load-misses,cache-misses,dTLB-load-misses,dTLB-store-misses,page-faults \
        -- true 2>&1 | sed -n '/Performance counter stats/,/seconds time elapsed/p' || true
else
    echo "perf tool not installed (optional: vdc_bench calls perf_event_open directly)."
fi
