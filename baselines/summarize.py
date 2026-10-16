"""Check and summarize the P1 baseline runs (results/p1/baseline_*.csv).

Correctness (every run must pass):
  - SAT/UNSAT and solved grid identical across all runs (same solver backend)
  - SAT/UNSAT equal to the PySAT reference; grid equal to the reference, or a
    valid alternative solution (valid Sudoku that keeps every predicted given)

Timing: T_handoff = T_conv (argmax + image) + T_xfer + T_sync per puzzle;
T_nn is not part of P1 (neural stage replayed from a trace). For the
double-buffered runs, the exposed handoff is (wall - sum T_sat) / N.

Usage: python summarize.py RESULTS_DIR
"""
import csv
import json
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]
RUNS = [("serial", "faithful"), ("serial", "lean"), ("double", "faithful"), ("double", "lean")]


def load(path):
    with open(path) as f:
        meta = dict(kv.split("=") for kv in f.readline().lstrip("# ").split())
        rows = list(csv.DictReader(f))
    return meta, rows


def valid_alternative(grid, givens):
    g = np.array(grid).reshape(9, 9)
    full = set(range(1, 10))
    ok = all(set(g[i]) == full and set(g[:, i]) == full for i in range(9))
    ok &= all(set(g[r:r + 3, c:c + 3].flatten()) == full for r in (0, 3, 6) for c in (0, 3, 6))
    return ok and all(givens[i] in (0, grid[i]) for i in range(81))


def main():
    out = Path(sys.argv[1])
    ref = np.load(REPO / "data" / "sudoku" / "reference.npz")
    ref_given, ref_solved = ref["predicted_givens"], ref["solved"]

    summary, first_grids = {}, None
    for mode, cfg in RUNS:
        meta, rows = load(out / f"baseline_{mode}_{cfg}.csv")
        cpu = float(meta["cycles_per_us"])
        n = len(rows)
        col = lambda k: np.array([float(r[k]) for r in rows])
        us = lambda k: col(k) / cpu
        conv = us("argmax_cyc") + us("image_cyc")
        handoff = conv + us("xfer_cyc") + us("sync_cyc")
        sat_us = us("sat_cyc")
        wall_us = float(meta["wall_cycles"]) / cpu

        grids = [[int(ch) for ch in r["grid"]] for r in rows]
        sats = [int(r["sat"]) for r in rows]
        if first_grids is None:
            first_grids = (grids, sats)
        same_as_first = grids == first_grids[0] and sats == first_grids[1]
        exact = alt = bad = 0
        for p, r in enumerate(rows):
            assert int(r["puzzle"]) == p
            ref_sat = int(not (ref_solved[p] == 0).all())
            if sats[p] != ref_sat:
                bad += 1
            elif not ref_sat or list(ref_solved[p]) == grids[p]:
                exact += 1
            elif valid_alternative(grids[p], list(ref_given[p])):
                alt += 1
            else:
                bad += 1

        med = lambda a: float(np.median(a))
        s = {
            "puzzles": n, "timer": meta["timer"], "cycles_per_us": cpu,
            "clear": meta["clear"], "xfer": meta["xfer"],
            "median_us": {"argmax": med(us("argmax_cyc")), "image": med(us("image_cyc")),
                          "conv": med(conv), "xfer": med(us("xfer_cyc")), "sync": med(us("sync_cyc")),
                          "stall": med(us("stall_cyc")), "sat": med(sat_us), "handoff": med(handoff)},
            "xfer_bytes_median": med(col("xfer_bytes")),
            "wall_us_per_puzzle": wall_us / n,
            "throughput_puzzles_per_s": n / (wall_us / 1e6),
            "check": {"identical_across_runs": same_as_first, "match_reference": exact,
                      "valid_alternative": alt, "wrong": bad},
        }
        if mode == "serial":
            s["handoff_fraction"] = float(handoff.sum() / (handoff.sum() + sat_us.sum()))
        else:
            s["exposed_handoff_us_per_puzzle"] = (wall_us - sat_us.sum()) / n
            s["exposed_handoff_fraction"] = float((wall_us - sat_us.sum()) / wall_us)
        summary[f"{mode}_{cfg}"] = s

    (out / "baselines_summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    lines = ["| Run | T_conv (argmax + image) | T_xfer | T_sync | T_sat | Handoff share | us/puzzle (wall) | Bytes moved | Checks |",
             "|---|---|---|---|---|---|---|---|---|"]
    for key, s in summary.items():
        m = s["median_us"]
        share = s.get("handoff_fraction", s.get("exposed_handoff_fraction"))
        chk = s["check"]
        ok = chk["identical_across_runs"] and chk["wrong"] == 0
        lines.append(
            f"| {key} | {m['conv']:.1f} ({m['argmax']:.2f} + {m['image']:.1f}) | {m['xfer']:.1f} | "
            f"{m['sync']:.1f} | {m['sat']:.1f} | {100 * share:.1f}% | {s['wall_us_per_puzzle']:.1f} | "
            f"{s['xfer_bytes_median']:,.0f} | {'PASS' if ok else 'FAIL'} "
            f"({chk['match_reference']}+{chk['valid_alternative']} alt) |")
    note = ("Times are medians in microseconds per puzzle. Handoff share is T_handoff / (T_handoff + T_sat) "
            "for serial runs and the exposed handoff fraction of wall time for double-buffered runs.")
    (out / "baselines_summary.md").write_text("\n".join(lines) + "\n\n" + note + "\n")
    print("\n".join(lines))
    print()
    print(note)
    if not all(s["check"]["identical_across_runs"] and s["check"]["wrong"] == 0 for s in summary.values()):
        sys.exit("correctness check FAILED")


if __name__ == "__main__":
    main()
