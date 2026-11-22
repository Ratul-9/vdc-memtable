#!/usr/bin/env bash
# Reserve a pool of 2 MiB huge pages for the hugetlb benchmarks (Linux, needs root).
#
#   sudo scripts/setup_hugepages.sh [pages]      default 128 pages = 256 MiB
#
# Needed at once: vdc memtable 32 pages (64 MiB), hash baseline up to 64 pages,
# skiplist baseline up to 48 pages (1M keys). 128 leaves headroom.
# The pool is not persistent across reboots. To make it permanent add
# "hugepages=128" to the kernel command line, or vm.nr_hugepages=128 to /etc/sysctl.conf.
set -euo pipefail

PAGES="${1:-128}"
KNOB=/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "ERROR: hugetlb pages are a Linux feature." >&2
    exit 1
fi
if [[ $EUID -ne 0 ]]; then
    echo "ERROR: run with sudo: sudo $0 $PAGES" >&2
    exit 1
fi
if [[ ! -w "$KNOB" ]]; then
    echo "ERROR: $KNOB not found: this kernel/CPU has no 2 MiB hugetlb pages." >&2
    exit 1
fi

# Free page cache and compact memory so the kernel can find contiguous 2 MiB blocks.
sync
echo 3 > /proc/sys/vm/drop_caches
echo 1 > /proc/sys/vm/compact_memory 2>/dev/null || true

echo "$PAGES" > "$KNOB"
GOT="$(cat "$KNOB")"

grep -E 'HugePages_(Total|Free)|Hugepagesize' /proc/meminfo
echo "Transparent huge pages: $(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo n/a)"
echo "  (the base-page benchmarks opt out of THP per mapping with MADV_NOHUGEPAGE)"

if (( GOT < PAGES )); then
    echo "ERROR: only $GOT of $PAGES huge pages could be reserved (memory is fragmented)." >&2
    echo "       Run this right after boot, or add hugepages=$PAGES to the kernel command line." >&2
    exit 1
fi
echo "OK: $GOT x 2 MiB huge pages reserved."
