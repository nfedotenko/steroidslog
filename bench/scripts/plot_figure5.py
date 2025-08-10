#!/usr/bin/env python3
import argparse, json, pathlib, re, sys
from collections import defaultdict

import matplotlib.pyplot as plt
import numpy as np

def parse_args():
    ap = argparse.ArgumentParser(description="Render NanoLog-style figure from Google Benchmark JSON.")
    ap.add_argument("--json", required=True, help="Path to benchmark JSON (benchmark_out).")
    ap.add_argument("--threads", type=int, choices=[1,4], default=1, help="Which 'threads' value to plot.")
    ap.add_argument("--out", required=True, help="Output PNG path.")
    ap.add_argument("--csv", default=None, help="Optional CSV summary path.")
    return ap.parse_args()

# bucket extraction and pretty names
bucket_order = ["staticString","stringConcat","singleInteger","twoIntegers","singleDouble","complexFormat"]
pretty_bucket = {
    "staticString":  "Static string",
    "stringConcat":  "Concat string",
    "singleInteger": "1×int",
    "twoIntegers":   "2×int",
    "singleDouble":  "1×double",
    "complexFormat": "Complex",
}

name_rx = re.compile(r'^BM_([^<]+)<Backend::([^>]+)>(?:/threads:\d+)?_mean$')

def load_table(path: pathlib.Path, threads: int):
    data = json.loads(path.read_text())
    tbl = {b: {} for b in bucket_order}
    loggers = set()

    for b in data.get("benchmarks", []):
        if b.get("run_type") != "aggregate" or b.get("aggregate_name") != "mean":
            continue
        if int(b.get("threads", 1)) != threads:
            continue
        m = name_rx.match(b.get("name",""))
        if not m:
            continue
        bucket_raw, backend = m.groups()
        if bucket_raw not in bucket_order:
            continue
        msgs = b.get("counters",{}).get("msgs/s", None)
        if msgs is None:
            msgs = b.get("msgs/s", None)
        if msgs is None:
            continue
        tbl[bucket_raw][backend] = float(msgs) / 1e6  # M logs/s
        loggers.add(backend)

    return tbl, sorted(loggers)

def plot_table(tbl, loggers, threads: int, out_path: pathlib.Path):
    x = np.arange(len(bucket_order))
    w = 0.8 / max(1, len(loggers))

    fig, ax = plt.subplots(figsize=(10, 5))
    for i, lg in enumerate(loggers):
        y = [tbl[b].get(lg, 0.0) for b in bucket_order]
        ax.bar(x + (i - (len(loggers)-1)/2)*w, y, width=w, label=lg)

    ax.set_xticks(x, [pretty_bucket.get(b, b) for b in bucket_order], rotation=15)
    ax.set_ylabel("Throughput (Millions of logs/second)")
    ax.set_title(f"Figure 5 style ({threads} thread{'s' if threads>1 else ''})")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)

def maybe_write_csv(tbl, loggers, out_csv: pathlib.Path):
    lines = ["bucket," + ",".join(loggers)]
    for b in bucket_order:
        row = [b] + [f"{tbl[b].get(lg, 0.0):.6f}" for lg in loggers]
        lines.append(",".join(row))
    out_csv.write_text("\n".join(lines))

def main():
    args = parse_args()
    in_path = pathlib.Path(args.json)
    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    tbl, loggers = load_table(in_path, args.threads)
    if not loggers:
        print(f"[warn] No data found for threads={args.threads} in {in_path}", file=sys.stderr)
        # Print a few names to help debug
        try:
            data = json.loads(in_path.read_text())
            names = [b.get("name","") for b in data.get("benchmarks", []) if b.get("run_type")=="aggregate"]
            print("[hint] Aggregate names in file:", *names[:10], sep="\n  ", file=sys.stderr)
        except Exception:
            pass
        sys.exit(2)

    plot_table(tbl, loggers, args.threads, out_path)
    print(f"[ok] Wrote {out_path}")

    if args.csv:
        maybe_write_csv(tbl, loggers, pathlib.Path(args.csv))

if __name__ == "__main__":
    main()
