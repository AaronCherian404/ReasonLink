"""Visual-Sudoku dataset: unique-solution puzzles with MNIST digits in the given cells.

For each puzzle:
  1. Make a random solved grid (random first row, solved with MiniSat, then
     random validity-preserving symmetries: digit relabeling, row/column
     swaps within bands/stacks, band/stack swaps, transpose).
  2. Remove clues in random order while the puzzle keeps a unique solution,
     stopping at a target clue count drawn from [--min-clues, --max-clues]
     (if digging stalls first, the puzzle keeps more clues; recorded).
  3. Draw an MNIST test image of the right digit for every given cell and run
     the NVDLA int8 LeNet stand-in on it.

Outputs in data/sudoku/ (regenerable, gitignored):
  base.cnf      base formula (DIMACS), identical for every puzzle
  puzzles.npz   puzzle, solution, clues, img_idx, ip2 (int8 logits), prob (int8)
  trace.bin     NVDLA output trace consumed by the C baselines (format below)

trace.bin layout (little-endian):
  header  char[4] "RLTR", u32 version=1, u32 num_puzzles, u32 cells=81,
          u32 classes=10, u32 stride=16, f32 ip2_scale, u32 reserved
  per puzzle: u8 given_mask[81], then int8 ip2[81][stride]
              (ip2 logits in bytes 0..9, bytes 10..15 zero; blanks all zero)
The 16-byte stride is our choice for the stand-in; the real NVDLA output
surface layout is to be confirmed when NVDLA runs on ESP.

Usage: python workload/dataset.py [--num 1000] [--seed 0]
"""
import argparse
import random
import struct
import time
from pathlib import Path

import numpy as np
import torch
from pysat.solvers import Solver

from nvdla_lenet import REPO, LeNet, load_mnist_test
from sudoku_cnf import base_clauses, given_literals, grid_from_model, var, write_dimacs

OUT = REPO / "data" / "sudoku"
N = 9
STRIDE = 16


def random_solution(rng, clauses):
    row = list(range(1, N + 1))
    rng.shuffle(row)
    with Solver(name="m22", bootstrap_with=clauses) as s:
        assert s.solve(assumptions=[var(0, c, d) for c, d in enumerate(row)])
        g = np.array(grid_from_model(s.get_model())).reshape(N, N)
    # Random symmetries that keep the grid valid.
    perm = np.array([0] + rng.sample(range(1, N + 1), N))
    g = perm[g]
    rows = [b * 3 + i for b in rng.sample(range(3), 3) for i in rng.sample(range(3), 3)]
    cols = [b * 3 + i for b in rng.sample(range(3), 3) for i in rng.sample(range(3), 3)]
    g = g[rows][:, cols]
    if rng.random() < 0.5:
        g = g.T
    return g.flatten()


def is_unique(solver, puzzle, solution, act):
    """True if `puzzle` has no solution other than `solution`.

    Adds (-act OR some blank cell differs from the solution) and solves under
    the givens + act; `act` is a fresh activation variable per call.
    """
    blanks = [i for i in range(N * N) if puzzle[i] == 0]
    if not blanks:
        return True
    solver.add_clause([-act] + [-var(i // N, i % N, int(solution[i])) for i in blanks])
    return not solver.solve(assumptions=given_literals(puzzle) + [act])


def dig(rng, clauses, solution, target):
    puzzle = solution.copy()
    act = N * N * N
    with Solver(name="m22", bootstrap_with=clauses) as s:
        for i in rng.sample(range(N * N), N * N):
            if int((puzzle > 0).sum()) <= target:
                break
            keep = puzzle[i]
            puzzle[i] = 0
            act += 1
            if not is_unique(s, puzzle, solution, act):
                puzzle[i] = keep
    return puzzle


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--num", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--min-clues", type=int, default=24)
    ap.add_argument("--max-clues", type=int, default=40)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    OUT.mkdir(parents=True, exist_ok=True)
    nv, clauses = base_clauses(3)
    write_dimacs(OUT / "base.cnf", nv, clauses, "sudoku 9x9 base formula (extended encoding)")

    t0 = time.time()
    puzzles, solutions = [], []
    for k in range(args.num):
        sol = random_solution(rng, clauses)
        puz = dig(rng, clauses, sol, rng.randint(args.min_clues, args.max_clues))
        puzzles.append(puz)
        solutions.append(sol)
        if (k + 1) % 100 == 0:
            print(f"{k + 1}/{args.num} puzzles ({time.time() - t0:.0f}s)")
    puzzles = np.array(puzzles, dtype=np.int8)
    solutions = np.array(solutions, dtype=np.int8)
    clues = (puzzles > 0).sum(1)

    # MNIST test images for the given cells, classified by the NVDLA stand-in.
    x_test, y_test = load_mnist_test()
    by_digit = {d: np.flatnonzero(y_test.numpy() == d) for d in range(1, 10)}
    img_idx = np.full(puzzles.shape, -1, dtype=np.int32)
    for p in range(len(puzzles)):
        for i in np.flatnonzero(puzzles[p]):
            img_idx[p, i] = rng.choice(by_digit[int(puzzles[p, i])])
    net = LeNet()
    flat = img_idx[img_idx >= 0]
    with torch.no_grad():
        ip2_g, prob_g, ip2_scale = net.forward_int8(x_test[torch.from_numpy(flat).long()])
    ip2 = np.zeros(puzzles.shape + (10,), dtype=np.int8)
    prob = np.zeros_like(ip2)
    ip2[img_idx >= 0] = ip2_g.numpy()
    prob[img_idx >= 0] = prob_g.numpy()

    np.savez_compressed(OUT / "puzzles.npz", puzzle=puzzles, solution=solutions, clues=clues,
                        img_idx=img_idx, ip2=ip2, prob=prob, ip2_scale=np.float32(ip2_scale),
                        seed=args.seed)

    with open(OUT / "trace.bin", "wb") as f:
        f.write(b"RLTR" + struct.pack("<5IfI", 1, len(puzzles), N * N, 10, STRIDE, ip2_scale, 0))
        padded = np.zeros(puzzles.shape + (STRIDE,), dtype=np.int8)
        padded[..., :10] = ip2
        for p in range(len(puzzles)):
            f.write((puzzles[p] > 0).astype(np.uint8).tobytes())
            f.write(padded[p].tobytes())

    cell_acc = (ip2[img_idx >= 0].astype(np.int32)[:, 1:].argmax(1) + 1 == puzzles[img_idx >= 0]).mean()
    print(f"{len(puzzles)} puzzles in {time.time() - t0:.0f}s; clues min/mean/max "
          f"{clues.min()}/{clues.mean():.1f}/{clues.max()}; given-cell accuracy (argmax 1..9) {cell_acc:.4f}")
    print("wrote", *(p.relative_to(REPO) for p in sorted(OUT.iterdir())))


if __name__ == "__main__":
    main()
