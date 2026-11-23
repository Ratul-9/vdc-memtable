#!/usr/bin/env python3
"""Combine the CSVs of one scale run (results/scale-<timestamp>/results_e*.csv) into summary tables.

Usage: scripts/scale/summarize_scale.py results/scale-<timestamp> > summary.md
Standard library only.
"""
import csv
import glob
import os
import sys


def keys_label(k):
    k = int(k)
    if k >= 1000000 and k % 1000000 == 0:
        return f"{k // 1000000}M"
    if k >= 1000 and k % 1000 == 0:
        return f"{k // 1000}K"
    return str(k)


def load(folder):
    rows = []
    for path in sorted(glob.glob(os.path.join(folder, "results_e*.csv"))):
        with open(path, newline="") as f:
            rows.extend(csv.DictReader(f))
    return rows


def val(r, col):
    v = r.get(col, "")
    return float(v) if v not in ("", None) else None


def pick(rows, **kw):
    for r in rows:
        if all(r.get(k) == v for k, v in kw.items()):
            return r
    return None


def table(title, header, body):
    print(f"\n### {title}\n")
    print("| " + " | ".join(header) + " |")
    print("|" + "---|" * len(header))
    for line in body:
        print("| " + " | ".join(line) + " |")


def fmt(x, nd=1):
    return "-" if x is None else f"{x:.{nd}f}"


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    rows = load(sys.argv[1])
    if not rows:
        print("no results_e*.csv files found", file=sys.stderr)
        return 1
    ran = [r for r in rows if r["status"] == "ran" and r["structure"] != "keygen"]
    keys = sorted({int(r["keys"]) for r in rows if r["keys"]})

    print("# Scale experiments: summary\n")
    print("Combined from the per-experiment CSV files. Times are ns per operation (medians), random order unless "
          "stated. Per-experiment reports (e1_*.md ... e5_*.md) have every table.")

    # Overview
    body = []
    for e in sorted({r["experiment"] for r in rows}):
        er = [r for r in rows if r["experiment"] == e and r["structure"] != "keygen"]
        n_ran = len({(r["structure"], r["pattern"], r["keys"]) for r in er if r["status"] == "ran"})
        n_skip = len([r for r in er if r["status"] == "skipped"])
        peak = max((int(r["mem_measured_bytes"] or 0) for r in er), default=0) / 2**30
        wrong = len([r for r in er if r["status"] == "ran" and r["ok"] != "1"])
        body.append([e, str(n_ran), str(n_skip), f"{peak:.2f} GiB", "✅ 0" if wrong == 0 else f"❌ {wrong}"])
    table("Overview", ["experiment", "structures run", "skipped (memory / time)", "peak memory measured",
                       "wrong results"], body)

    # 1. Flat scaling
    body = []
    for k in keys:
        r = {op: pick(ran, structure="flat", page_mode="base", pattern="dense", order="rand", keys=str(k), op=op)
             for op in ("insert", "get_hit", "get_miss", "update")}
        if not any(r.values()):
            continue
        body.append([keys_label(k)] + [fmt(val(r[op], "ns_op")) if r[op] else "-" for op in r] +
                    [fmt(val(r["get_hit"], "instr_op")) if r["get_hit"] else "-"])
    if body:
        table("1. Flat array at scale (dense keys, random order, base pages)",
              ["keys", "insert", "get_hit", "get_miss", "update", "get_hit instructions"], body)

    # 2. Row-table cost
    for order in ("rand", "seq"):
        body = []
        for k in keys:
            f = pick(ran, structure="flat", page_mode="base", pattern="dense", order=order, keys=str(k), op="get_hit")
            if f is None:
                continue
            line = [keys_label(k), fmt(val(f, "ns_op"))]
            for s in ("rows-4K", "rows-64K", "rows-2M"):
                r = pick(ran, structure=s, page_mode="base", pattern="dense", order=order, keys=str(k), op="get_hit")
                if r is None:
                    line.append("-")
                else:
                    d = val(r, "ns_op") - val(f, "ns_op")
                    llc_r, llc_f = val(r, "llc_miss_op"), val(f, "llc_miss_op")
                    extra = f" ({llc_r - llc_f:+.2f} LLC)" if llc_r is not None and llc_f is not None else ""
                    line.append(f"{val(r, 'ns_op'):.1f} ({d:+.1f}){extra}")
            body.append(line)
        if body:
            table(f"2. Cost of the row table: get_hit, dense keys, {order} order (cell = ns/op (Δ vs flat))",
                  ["keys", "flat", "rows-4K (table in L3)", "rows-64K (table in L2)", "rows-2M (table in L1)"], body)

    # 3. Density
    dens = [r for r in rows if r["experiment"] == "3"]
    if dens:
        body = []
        for k in keys:
            for pat in ("dense", "clustered", "sparse"):
                line = [keys_label(k), pat]
                any_cell = False
                for s in ("flat", "rows-4K", "rows-64K", "rows-2M"):
                    r = [x for x in dens if x["structure"] == s and x["pattern"] == pat and x["keys"] == str(k)]
                    if not r:
                        line.append("-")
                        continue
                    any_cell = True
                    hit = [x for x in r if x["op"] == "get_hit"]
                    if hit:
                        per_key = int(hit[0]["mem_used_bytes"]) / k
                        line.append(f"{per_key:.0f} B/key, {val(hit[0], 'ns_op'):.1f} ns")
                    else:
                        need = max(int(r[0]["mem_plan_bytes"]) - 256 * 2**20, 0) / k
                        line.append(f"skipped (needs {need:.0f} B/key)")
                if any_cell:
                    body.append(line)
        table("3. Key density: memory per key and get_hit time (random order)",
              ["keys", "pattern", "flat", "rows-4K", "rows-64K", "rows-2M"], body)

    # 4. Huge vs base
    body = []
    for k in keys:
        for s in ("flat", "rows-64K", "rows-2M"):
            h = pick(ran, structure=s, page_mode="hugetlb", pattern="dense", order="rand", keys=str(k), op="get_hit")
            b = pick(ran, structure=s, page_mode="base", pattern="dense", order="rand", keys=str(k), op="get_hit")
            if h is None:
                continue
            def dt(r):
                if r is None:
                    return None
                a, b = val(r, "dtlb_load_miss_op"), val(r, "dtlb_store_miss_op")
                if a is None and b is None:
                    return None  # counter group not measured
                return (a or 0) + (b or 0)
            body.append([keys_label(k), s, fmt(val(b, "ns_op")) if b else "-", fmt(val(h, "ns_op")),
                         fmt(dt(b), 3), fmt(dt(h), 3)])
    if body:
        table("4. Huge pages vs base pages: get_hit, dense keys, random order",
              ["keys", "structure", "base ns", "hugetlb ns", "base dTLB/op", "hugetlb dTLB/op"], body)

    # 5. Baselines
    for op in ("get_hit", "insert", "get_miss"):
        body = []
        for k in keys:
            line = [keys_label(k)]
            found = False
            for s in ("flat", "rows-2M", "hash", "skiplist"):
                r = pick(ran, structure=s, page_mode="base", pattern="dense", order="rand", keys=str(k), op=op)
                line.append(fmt(val(r, "ns_op")) if r else "-")
                found |= r is not None
            if found:
                body.append(line)
        if body:
            table(f"5. Against the baselines: {op}, dense keys, random order (ns/op)",
                  ["keys", "flat", "rows-2M", "hash table", "skip list"], body)

    # O(1)
    body = []
    for s in sorted({r["structure"] for r in ran}):
        for mode in ("base", "hugetlb"):
            worst = None
            for pat in ("dense", "clustered", "sparse"):
                for order in ("seq", "rand"):
                    for op in ("insert", "get_hit", "get_miss", "update"):
                        vals = [val(r, "instr_op") for r in ran if r["structure"] == s and r["page_mode"] == mode and
                                r["pattern"] == pat and r["order"] == order and r["op"] == op and val(r, "instr_op")]
                        if len(vals) >= 2:
                            ratio = max(vals) / min(vals)
                            worst = ratio if worst is None else max(worst, ratio)
            if worst is not None:
                body.append([s, mode, f"{worst:.2f}", "✅" if worst <= 1.10 else "❌"])
    if body:
        table("O(1) check: worst max/min instructions per op across key counts (≤ 1.10)",
              ["structure", "pages", "worst ratio", "O(1)"], body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
