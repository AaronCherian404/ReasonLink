"""Sudoku as CNF: base formula (identical for every puzzle) + givens as unit literals.

Variable numbering (1-based, DIMACS): x(r, c, d) = r*n*n + c*n + (d-1) + 1 for
row r, column c in [0, n) and digit d in [1, n], n = box*box. For 9x9 this is
var = 81r + 9c + d, so the 729 variables are ordered cell-major, which is
what the converters (CPU now, ReasonLink RTL later) compute from
(cell index, argmax digit).

Base formula ("extended" encoding):
  cell:   each cell has at least one digit, and at most one (pairwise)
  row / column / box: each digit appears at least once, and at most once (pairwise)
For 9x9: 729 vars, 324 at-least-one clauses + 4 * 81 * 36 = 11,664 binary
clauses = 11,988 clauses.
"""
from itertools import combinations
from pathlib import Path


def var(r, c, d, n=9):
    return r * n * n + c * n + (d - 1) + 1


def decode_var(v, n=9):
    v -= 1
    return v // (n * n), (v // n) % n, v % n + 1


def base_clauses(box=3):
    n = box * box
    groups = []
    for r in range(n):
        for c in range(n):
            groups.append([var(r, c, d, n) for d in range(1, n + 1)])              # cell
    for d in range(1, n + 1):
        for r in range(n):
            groups.append([var(r, c, d, n) for c in range(n)])                     # row
        for c in range(n):
            groups.append([var(r, c, d, n) for r in range(n)])                     # column
        for br in range(box):
            for bc in range(box):
                groups.append([var(br * box + i, bc * box + j, d, n)
                               for i in range(box) for j in range(box)])         # box
    clauses = [list(g) for g in groups]                                             # at least one
    for g in groups:
        clauses.extend([-a, -b] for a, b in combinations(g, 2))                    # at most one
    return n * n * n, clauses


def given_literals(grid, n=9):
    """grid: length n*n sequence, 0 = blank. Returns positive unit literals."""
    return [var(i // n, i % n, int(d), n) for i, d in enumerate(grid) if d]


def write_dimacs(path, num_vars, clauses, comment=None):
    lines = [f"c {comment}"] if comment else []
    lines.append(f"p cnf {num_vars} {len(clauses)}")
    lines.extend(" ".join(map(str, cl)) + " 0" for cl in clauses)
    Path(path).write_text("\n".join(lines) + "\n")


def grid_from_model(model, n=9):
    grid = [0] * (n * n)
    for lit in model:
        if 0 < lit <= n * n * n:
            r, c, d = decode_var(lit, n)
            grid[r * n + c] = d
    return grid


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--box", type=int, default=3, help="3 for 9x9, 4 for 16x16")
    ap.add_argument("--out", default=None, help="write the base formula as DIMACS")
    args = ap.parse_args()
    nv, cls = base_clauses(args.box)
    print(f"{args.box ** 2}x{args.box ** 2}: {nv} vars, {len(cls)} clauses, "
          f"{sum(map(len, cls))} literal occurrences")
    if args.out:
        write_dimacs(args.out, nv, cls, f"sudoku base formula, box={args.box}")
