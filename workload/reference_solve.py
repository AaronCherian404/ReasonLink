"""Functional reference for the visual-Sudoku pipeline (software only).

For every puzzle: argmax over digits 1..9 of the int8 ip2 logits in each given
cell (ties -> lowest digit), unit literals for the predicted givens, solve the
base formula + units with pysat. The same puzzle is also solved with the true
givens ("oracle") to separate classifier errors from solver behaviour.

These solutions are the equivalence reference every interface (serial,
double-buffered, ReasonLink) must reproduce exactly.

Outputs:
  results/p1/reference_solve.csv     per-puzzle outcome and solver stats
  results/p1/reference_summary.json  aggregate accuracy / UNSAT rate / stats
  data/sudoku/reference.npz          predicted givens and solved grids
  data/sudoku/reference.bin          the same, for the C tools (handoff/trace.h)

Usage: python workload/reference_solve.py [--solver m22]
"""
import argparse
import csv
import json
import struct
import time

import numpy as np
from pysat.solvers import Solver

from nvdla_lenet import REPO
from sudoku_cnf import base_clauses, given_literals, grid_from_model

DATA = REPO / "data" / "sudoku"
RESULTS = REPO / "results" / "p1"


def predict_givens(puzzle, ip2):
    """Argmax over classes 1..9 for given cells; 0 for blanks."""
    pred = ip2[..., 1:10].astype(np.int32).argmax(-1) + 1
    return np.where(puzzle > 0, pred, 0).astype(np.int8)


def solve(clauses, grid, solver_name):
    with Solver(name=solver_name, bootstrap_with=clauses) as s:
        lits = given_literals(grid)
        for lit in lits:
            s.add_clause([lit])
        t0 = time.perf_counter()
        sat = s.solve()
        dt = time.perf_counter() - t0
        stats = s.accum_stats() or {}
        sol = grid_from_model(s.get_model()) if sat else [0] * 81
    return sat, np.array(sol, dtype=np.int8), dt, stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--solver", default="m22", help="pysat solver name (m22 = MiniSat 2.2)")
    args = ap.parse_args()

    d = np.load(DATA / "puzzles.npz")
    puzzle, solution, ip2, clues = d["puzzle"], d["solution"], d["ip2"], d["clues"]
    _, clauses = base_clauses(3)
    pred = predict_givens(puzzle, ip2)
    RESULTS.mkdir(parents=True, exist_ok=True)

    rows, solved = [], np.zeros_like(solution)
    for p in range(len(puzzle)):
        wrong = int(((pred[p] != puzzle[p]) & (puzzle[p] > 0)).sum())
        sat, sol, dt, st = solve(clauses, pred[p], args.solver)
        o_sat, o_sol, o_dt, o_st = solve(clauses, puzzle[p], args.solver)
        assert o_sat and (o_sol == solution[p]).all(), f"oracle mismatch on puzzle {p}"
        solved[p] = sol
        rows.append({
            "puzzle": p, "clues": int(clues[p]), "misclassified_givens": wrong,
            "sat": int(sat), "correct": int(sat and (sol == solution[p]).all()),
            "solve_s": f"{dt:.6f}", "decisions": st.get("decisions", ""),
            "propagations": st.get("propagations", ""), "conflicts": st.get("conflicts", ""),
            "oracle_solve_s": f"{o_dt:.6f}", "oracle_decisions": o_st.get("decisions", ""),
            "oracle_propagations": o_st.get("propagations", ""), "oracle_conflicts": o_st.get("conflicts", ""),
        })

    with open(RESULTS / "reference_solve.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    np.savez_compressed(DATA / "reference.npz", predicted_givens=pred, solved=solved)
    # Same content for the C tools: "RLRF", u32 version=1, u32 N, then per
    # puzzle u8 sat, i8 solved[81] (zeros if UNSAT), i8 predicted_givens[81].
    with open(DATA / "reference.bin", "wb") as f:
        f.write(b"RLRF" + struct.pack("<2I", 1, len(puzzle)))
        for p in range(len(puzzle)):
            f.write(bytes([rows[p]["sat"]]) + solved[p].tobytes() + pred[p].tobytes())

    n = len(rows)
    col = lambda k: np.array([float(r[k]) for r in rows])
    given_cells = int((puzzle > 0).sum())
    summary = {
        "solver": args.solver,
        "puzzles": n,
        "clues_mean": float(clues.mean()),
        "given_cell_accuracy": 1 - float(col("misclassified_givens").sum()) / given_cells,
        "puzzles_all_givens_correct": float((col("misclassified_givens") == 0).mean()),
        "puzzle_accuracy": float(col("correct").mean()),
        "unsat_rate": float(1 - col("sat").mean()),
        "sat_but_wrong_rate": float(((col("sat") == 1) & (col("correct") == 0)).mean()),
        "mean_solve_s": float(col("solve_s").mean()),
        "mean_decisions": float(col("decisions").mean()),
        "mean_propagations": float(col("propagations").mean()),
        "mean_conflicts": float(col("conflicts").mean()),
        "oracle_mean_conflicts": float(col("oracle_conflicts").mean()),
    }
    (RESULTS / "reference_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for k, v in summary.items():
        print(f"{k:28s} {v:.4f}" if isinstance(v, float) else f"{k:28s} {v}")


if __name__ == "__main__":
    main()
