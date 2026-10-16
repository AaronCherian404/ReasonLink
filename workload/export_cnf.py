"""Write full DIMACS files (base formula + predicted givens as unit clauses)
for selected puzzles, i.e. exactly what SAT-Accel's native host flow parses.

Usage: python workload/export_cnf.py --out DIR [--puzzles 0 1 2] [--unsat 1]
  --unsat K   also export the first K puzzles whose predicted givens are UNSAT
"""
import argparse
from pathlib import Path

import numpy as np

from nvdla_lenet import REPO
from sudoku_cnf import base_clauses, given_literals, write_dimacs

DATA = REPO / "data" / "sudoku"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--puzzles", type=int, nargs="*", default=[0, 1, 2])
    ap.add_argument("--unsat", type=int, default=1)
    args = ap.parse_args()

    pred = np.load(DATA / "reference.npz")["predicted_givens"]
    solved = np.load(DATA / "reference.npz")["solved"]
    unsat = [int(p) for p in np.flatnonzero((solved == 0).all(1))[: args.unsat]]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    nv, base = base_clauses(3)
    for p in list(args.puzzles) + unsat:
        units = [[lit] for lit in given_literals(pred[p])]
        tag = "unsat" if p in unsat else "sat"
        write_dimacs(out / f"sudoku_p{p}_{tag}.cnf", nv, base + units, f"puzzle {p}, predicted givens")
        print(out / f"sudoku_p{p}_{tag}.cnf")


if __name__ == "__main__":
    main()
