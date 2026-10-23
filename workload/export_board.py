"""Export the Sudoku workload for the board baselines (Ariane + NVDLA + SAT-Accel).

On the board, NVDLA classifies every given cell from its MNIST image (where
P1 replayed the int8 emulator's trace). This writes data/board/:

  cells.bin       the given cells' images and the emulator's expected logits
  base.cnf        copy of data/sudoku/base.cnf
  reference.bin   copy of data/sudoku/reference.bin (PySAT reference)

cells.bin (little endian):
  header  "RLBC", u32 version=1, u32 num_puzzles, u32 num_images,
          u32 img_bytes=784, u32 classes=10
  images  num_images x 784 u8: MNIST test images used by any puzzle, stored
          once each (raw pixels, white on black; NVDLA's runtime maps p to
          round(p*127/255), as the emulator does)
  per puzzle:
          i16 image[81]       index into images, -1 for a blank cell
          i8  ip2[81][10]     the int8 emulator's ip2 logits for that image
                              (zero for blanks): the expected NVDLA output

Usage (from workload/): python export_board.py
"""
import shutil
import struct

import numpy as np

from nvdla_lenet import REPO, load_mnist_test

SUDOKU = REPO / "data" / "sudoku"
OUT = REPO / "data" / "board"


def main():
    d = np.load(SUDOKU / "puzzles.npz")
    img_idx, ip2 = d["img_idx"], d["ip2"]
    x_test, _ = load_mnist_test()
    x_test = x_test.numpy()

    used = np.unique(img_idx[img_idx >= 0])
    remap = np.full(len(x_test), -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    local = np.where(img_idx >= 0, remap[np.maximum(img_idx, 0)], -1).astype(np.int16)

    OUT.mkdir(parents=True, exist_ok=True)
    with open(OUT / "cells.bin", "wb") as f:
        f.write(b"RLBC" + struct.pack("<5I", 1, len(img_idx), len(used), 784, 10))
        f.write(x_test[used].astype(np.uint8).tobytes())
        for p in range(len(img_idx)):
            f.write(local[p].astype("<i2").tobytes())
            f.write(ip2[p].astype(np.int8).tobytes())
    for name in ("base.cnf", "reference.bin"):
        shutil.copy(SUDOKU / name, OUT / name)
    size = (OUT / "cells.bin").stat().st_size
    print(f"{OUT}/cells.bin: {len(img_idx)} puzzles, {len(used)} distinct images, "
          f"{int((img_idx >= 0).sum())} given cells, {size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
