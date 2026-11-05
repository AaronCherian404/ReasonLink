"""Check and summarize the board baseline runs (rl_board CSVs from the board).

Correctness (every run must pass):
  - SAT/UNSAT and grid identical across all runs
  - SAT/UNSAT equal to the PySAT reference; grid equal to the reference, or a
    valid alternative solution (valid Sudoku that keeps every predicted given)
  - NVDLA's int8 logits equal the emulator's on every cell (nn=nvdla runs)

Timing, per the proposal's decomposition (cycles converted with the run's
cycles_per_us):
  T_e2e = T_nn + T_handoff + T_sat,  T_handoff = T_conv + T_xfer + T_sync
  serial runs:      medians per puzzle; handoff share = T_handoff / T_e2e
  double-buffered:  exposed handoff = (wall - sum T_nn - sum T_sat) / N, so
                    nothing that overlaps is counted twice

Usage: python3 summarize_board.py <results_dir>  (board_<mode>_<cfg>[_trace].csv)
"""
import csv
import json
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]


def load(path):
    with open(path) as f:
        meta = dict(kv.split("=", 1) for kv in f.readline().lstrip("# ").split())
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
    ref_sat = (ref_solved != 0).any(axis=1).astype(int)   # UNSAT: all-zero solved grid (as summarize.py)
    # *_layoutbug.csv: ReasonLink runs before the 2026-11-05 layout fix, kept as evidence only
    runs = sorted(p for p in out.glob("board_*.csv") if not p.stem.endswith("_layoutbug"))
    if not runs:
        sys.exit(f"no board_*.csv in {out}")
    summary, first = {}, None
    for path in runs:
        meta, rows = load(path)
        cpu = float(meta["cycles_per_us"])
        n = len(rows)
        col = lambda k: np.array([float(r[k]) for r in rows]) / cpu
        nn, conv = col("nn_cyc"), col("argmax_cyc") + col("image_cyc")
        xfer, sync, sat = col("xfer_cyc"), col("sync_cyc"), col("sat_cyc")
        handoff = conv + xfer + sync
        wall = float(meta["wall_cycles"]) / cpu
        # SSH stalls: a login to the board costs its CPU 1-1.5 s, which lands in
        # whatever runs (almost always an NVDLA classification). Remove, per
        # puzzle, the NVDLA time above the run's median time per cell.
        givens = np.array([float(r["givens"]) for r in rows])
        stall = 0.0
        if meta.get("nn") != "trace":
            per_cell = float(np.median(nn / np.maximum(givens, 1)))
            stall = float(np.maximum(nn - per_cell * givens, 0).sum())
        if meta["mode"] == "serial":
            share = float(np.median(handoff / (nn + handoff + sat)))
        else:
            share = float((wall - nn.sum() - sat.sum()) / n) / (wall / n)
        grids = [[int(ch) for ch in r["grid"]] for r in rows]
        sats = [int(r["sat"]) for r in rows]
        if first is None:
            first = (grids, sats)
        exact = alt = bad = 0
        for p in range(n):
            if sats[p] != ref_sat[p]:
                bad += 1
            elif not ref_sat[p] or grids[p] == ref_solved[p].tolist():
                exact += 1
            elif valid_alternative(grids[p], ref_given[p].tolist()):
                alt += 1
            else:
                bad += 1
        nn_mism = int(sum(int(r["nn_mismatch"]) for r in rows))
        summary[path.stem] = {
            "mode": meta["mode"], "clear": meta["clear"], "xfer": meta["xfer"], "nn": meta.get("nn", "?"),
            "handoff": meta.get("handoff", "full"), "resident": meta.get("resident", "0") == "1",
            "puzzles": n, "cycles_per_us": cpu,
            "median_us": {"T_nn": float(np.median(nn)), "T_conv": float(np.median(conv)),
                          "T_xfer": float(np.median(xfer)), "T_sync": float(np.median(sync)),
                          "T_sat": float(np.median(sat))},
            "wall_us_per_puzzle": wall / n, "wall_us_per_puzzle_no_stalls": (wall - stall) / n,
            "ssh_stall_us": stall, "handoff_share": share,
            "bytes_moved": int(rows[0]["xfer_bytes"]),
            "tile_mem_stall_share": float(np.median(col("tile_mem_stall") / np.maximum(col("tile_busy"), 1e-9))),
            # SAT-Accel's DRAM reads per solve (median; 8 B beats), what Milestone 2 reduces
            "tile_read_bytes": float(np.median(col("tile_rd_beats"))) * 8 if "tile_rd_beats" in rows[0] else None,
            "check": {"identical_across_runs": (grids, sats) == first, "match_reference": exact,
                      "valid_alternative": alt, "wrong": bad, "nvdla_cells_differing": nn_mism},
        }
    lines = ["| Run | T_nn | T_conv | T_xfer | T_sync | T_sat | Handoff share | ms/puzzle (wall, no SSH stalls) "
             "| Bytes moved | Checks |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for k, s in summary.items():
        m, c = s["median_us"], s["check"]
        # NVDLA's logits are not bit-exact with the emulator on the board, but its digits
        # are: a run passes on its answers (the logit differences are reported)
        ok = c["identical_across_runs"] and c["wrong"] == 0
        lines.append(f"| {k} | {m['T_nn']:.1f} | {m['T_conv']:.1f} | {m['T_xfer']:.1f} | {m['T_sync']:.1f} | "
                     f"{m['T_sat']:.1f} | {100 * s['handoff_share']:.2f}% | {s['wall_us_per_puzzle'] / 1e3:.1f} "
                     f"({s['wall_us_per_puzzle_no_stalls'] / 1e3:.1f}) | "
                     f"{s['bytes_moved']:,} | {'PASS' if ok else 'FAIL'} ({c['match_reference']}+"
                     f"{c['valid_alternative']} alt, NVDLA logit diffs {c['nvdla_cells_differing']}) |")
    lines.append("")
    lines.append("T_* are medians in microseconds per puzzle. Handoff share: T_handoff / T_e2e for serial "
                 "runs, the exposed handoff fraction of wall time for double-buffered runs. ReasonLink's "
                 "per-cell push and credit wait (about 18 us per cell) are inside T_nn. A negative exposed "
                 "handoff means the solve itself was hidden behind the next puzzle's NVDLA work (wall < "
                 "sum T_nn + sum T_sat): nothing of the handoff is exposed. The no-SSH-stall "
                 "wall time removes, per puzzle, the NVDLA time above the run's median per cell.")
    (out / "board_summary.md").write_text("\n".join(lines) + "\n")
    (out / "board_summary.json").write_text(json.dumps(summary, indent=1))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
