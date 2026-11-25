#!/usr/bin/env python3
"""Summarize the NVDLA driver profile (board/ops/rl_nvprof.sh, 2026-11-24).

Usage: python3 board/nvprof_summary.py DIR [DIR_OLD]
  DIR      folder with nvprof_*.csv from the board
  DIR_OLD  optional: results/p2/board/csv, to compare against the 11-05 runs on the
           same puzzles (the first N of board_serial_*.csv)
Prints per-cell NVDLA time, wall time per puzzle, and stage medians per run.
Every run is also checked against its 11-05 counterpart: SAT flag, grid, decisions.
"""
import csv, os, statistics, sys


def load(path):
    with open(path) as f:
        hdr = f.readline().lstrip('# ').split()
        meta = dict(kv.split('=', 1) for kv in hdr if '=' in kv)
        rows = list(csv.DictReader(f))
    return meta, rows


def summary(meta, rows):
    mhz = float(meta['cycles_per_us'])
    ms = lambda c: c / mhz / 1000.0
    per_cell = [int(r['nn_cyc']) / int(r['givens']) for r in rows]
    med = lambda k: statistics.median(int(r[k]) for r in rows)
    handoff = statistics.median(
        (int(r['argmax_cyc']) + int(r['image_cyc']) + int(r['xfer_cyc'])) /
        (int(r['nn_cyc']) + int(r['argmax_cyc']) + int(r['image_cyc']) + int(r['xfer_cyc'])
         + int(r['sync_cyc'])) for r in rows)
    return {
        'n': len(rows),
        'wall_s_per_puzzle': ms(int(meta['wall_cycles'])) / 1000 / len(rows),
        'nn_ms_per_cell_median': ms(statistics.median(per_cell)),
        'nn_ms_per_cell_min': ms(min(per_cell)),
        'nn_ms_per_cell_max': ms(max(per_cell)),
        'image_ms': ms(med('image_cyc')), 'xfer_ms': ms(med('xfer_cyc')),
        'sat_ms': ms(med('sat_cyc')),
        'handoff_share_median': handoff,
        'nn_mismatch_cells': sum(int(r['nn_mismatch']) for r in rows),
    }


def same_answers(a, b):
    key = lambda r: (r['puzzle'], r['sat'], r['grid'], r['decisions'], r['conflicts'])
    bm = {r['puzzle']: key(r) for r in b}
    return sum(1 for r in a if bm.get(r['puzzle']) == key(r)), len(a)


OLD = {'verbose_delta': 'board_serial_delta.csv', 'quiet_delta': 'board_serial_delta.csv',
       'quiet_reasonlink': 'board_serial_reasonlink.csv', 'quiet_faithful': 'board_serial_faithful.csv'}

d = sys.argv[1]
old = sys.argv[2] if len(sys.argv) > 2 else None
for name in ['verbose_delta', 'quiet_delta', 'quiet_reasonlink', 'quiet_faithful']:
    p = os.path.join(d, f'nvprof_{name}.csv')
    if not os.path.exists(p):
        print(f'{name}: missing'); continue
    meta, rows = load(p)
    s = summary(meta, rows)
    line = (f"{name:17s} n={s['n']:3d} wall {s['wall_s_per_puzzle']:.3f} s/puzzle | NVDLA/cell median "
            f"{s['nn_ms_per_cell_median']:.2f} ms [{s['nn_ms_per_cell_min']:.2f}-{s['nn_ms_per_cell_max']:.2f}] | "
            f"image {s['image_ms']:.1f} xfer {s['xfer_ms']:.2f} sat {s['sat_ms']:.2f} ms | "
            f"handoff share {100 * s['handoff_share_median']:.2f}% | nn_mismatch cells {s['nn_mismatch_cells']}")
    if old:
        _, orows = load(os.path.join(old, OLD[name]))
        ok, n = same_answers(rows, orows)
        line += f' | answers = 11-05 run: {ok}/{n}'
    print(line)
