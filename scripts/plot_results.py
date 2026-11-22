#!/usr/bin/env python3
"""Rebuild markdown pivot tables from vdc_bench CSV files (tables only, standard library).

Usage:
  scripts/plot_results.py results/<run>/results_vdc.csv [more.csv ...]
        [--metric ns_op] [--pages base] [--pattern rand] [--op get_hit]

Rows = key count, columns = variant (j or structure). Several CSV files can be
combined, e.g. runs from two machines; the file name is then added to the
column label.

Metrics: ns_op cycles_op instr_op l1d_load_miss_op llc_miss_op
         dtlb_load_miss_op dtlb_store_miss_op dtlb_miss_op page_faults
"""
import argparse
import csv
import os
import sys
from collections import OrderedDict


def keys_label(k):
    k = int(k)
    if k >= 1000000 and k % 1000000 == 0:
        return f"{k // 1000000}M"
    if k >= 1000 and k % 1000 == 0:
        return f"{k // 1000}K"
    return str(k)


def metric_value(row, metric):
    if metric == "dtlb_miss_op":
        a, b = row.get("dtlb_load_miss_op", ""), row.get("dtlb_store_miss_op", "")
        if a == "" and b == "":
            return None
        return (float(a) if a else 0.0) + (float(b) if b else 0.0)
    v = row.get(metric, "")
    return float(v) if v != "" else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--metric", default="ns_op")
    ap.add_argument("--pages")
    ap.add_argument("--pattern")
    ap.add_argument("--op")
    args = ap.parse_args()

    rows = []
    for path in args.csv:
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                r["_file"] = os.path.basename(path)
                rows.append(r)
    if not rows:
        print("no rows", file=sys.stderr)
        return 1

    multi = len(args.csv) > 1
    groups = OrderedDict()
    for r in rows:
        if args.pages and r["page_mode"] != args.pages:
            continue
        if args.pattern and r["pattern"] != args.pattern:
            continue
        if args.op and r["op"] != args.op:
            continue
        groups.setdefault((r["page_mode"], r["pattern"], r["op"]), []).append(r)

    fmt = "{:.1f}" if args.metric in ("ns_op", "cycles_op", "instr_op") else "{:.3f}"
    for (pages, pattern, op), grp in groups.items():
        variants = list(OrderedDict.fromkeys(
            (r["variant"] + (f" [{r['_file']}]" if multi else "")) for r in grp))
        keys = sorted({int(r["keys"]) for r in grp})
        cell = {}
        for r in grp:
            v = metric_value(r, args.metric)
            label = r["variant"] + (f" [{r['_file']}]" if multi else "")
            if v is not None:
                cell[(int(r["keys"]), label)] = v
        print(f"\n### {pages} pages / {pattern} / {op} — {args.metric}\n")
        print("| keys | " + " | ".join(variants) + " |")
        print("|---|" + "---:|" * len(variants))
        for k in keys:
            vals = [fmt.format(cell[(k, v)]) if (k, v) in cell else "-" for v in variants]
            print(f"| {keys_label(k)} | " + " | ".join(vals) + " |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
