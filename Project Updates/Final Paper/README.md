# Final Paper — working draft

Main source: `neural-symbolic-final.tex`. The draft uses the same IEEE conference
class and author information as the proposal and P1 report. It reflects evidence
available on **November 5, 2026**; it does not assume a final page limit.

The introduction uses paper-style statements of the design, findings, and
contributions. Measurement qualifications and the distinction between measured
and interference-adjusted times are documented in the evaluation section;
draft-status tracking remains here and in the marked TODOs.

```sh
latexmk -pdf -interaction=nonstopmode -halt-on-error neural-symbolic-final.tex
```

The compiled PDF is placed beside the main source; auxiliary files go in `build/`.
Upload this entire folder to Overleaf and select `neural-symbolic-final.tex` as
the main document. Generated chart data, tables, numeric macros, the class, and
bibliographies are included, so compilation does not require the full repository.

## Draft structure

- `sections/introduction.tex`: research question and contributions.
- `sections/background.tex`: encoding, timing definitions, and hypotheses.
- `sections/design.tex`: literal generation, DRAM reuse, state restoration, overlap.
- `sections/implementation.tex`: actual ESP/NVDLA/SAT-Accel implementation.
- `sections/evaluation.tex`: preliminary board results, RTL checks, correctness.
- `sections/discussion.tex`: interpretation and limits of the current evidence.
- `sections/conclusion.tex`: conclusions supported by current evidence.
- `sections/appendix.tex`: currently empty; reserved for appendix content.
- `figures/`: editable TikZ/PGFPlots sources and generated result assets.

## Regenerate result figures and tables

From this folder, with the complete repository present:

```sh
python3 figures/generate-results.py
latexmk -pdf -interaction=nonstopmode -halt-on-error neural-symbolic-final.tex
```

The script uses only Python's standard library and reads
`../../results/p2/board/csv/board_summary.json`. It generates serial and
double-buffered wall-time data, the serial timing table, numeric macros, and a
source SHA-256 manifest. It does not run experiments or change the board summary.
You can select a separately archived summary with `--summary PATH`.

## Configuration names

- **Faithful full image**: follows the original SAT-Accel host clearing/transfer
  policy, including unused allocated capacity; rebuilds the image each puzzle.
- **Lean full image**: clears previously used regions and transfers current used
  regions; still rebuilds the entire used image each puzzle.
- **CPU-generated delta**: CPU software computes argmax, encodes fixed-decision
  literals, and writes the compact solver update.
- **Hardware-generated delta (ReasonLink)**: Verilog RTL performs the same
  transformation and writes the compact update; CPU descriptors coordinate it.
- **+ on-chip base reuse**: an independent HLS solver optimization available to
  either delta path, retaining structural solver state between puzzles.

Charts abbreviate these as **CPU delta**, **HW delta**, and **+B**. The timing
table reports on-chip base reuse in its own column. Original result keys
(`delta`, `reasonlink`, and `_resident`) are preserved for reproducibility.

## Evidence and interpretation

The main evidence is the board summary and its raw CSVs, `../../PROGRESS.md`, and
the functional/RTL logs in `../../results/p2/m2/`. The draft reports raw wall
times alongside the existing interference-adjusted estimates. Estimates are
explicitly labeled and do not stand in for clean repeated measurements.

The paper distinguishes interface equivalence from ground-truth accuracy. The
summary's 98 reference-equivalent cases include matching UNSAT outcomes; two
additional cases are valid alternative solutions. The current results support
large savings from compact updates/base reuse, but do not establish an
end-to-end hardware delta generation advantage over the CPU-generated delta path.

Core background references are inherited from the earlier reports. Initial
source checks used the [SAT-Accel author repository](https://github.com/milo168/FPGA25_SAT_Accel),
the [ESP paper](https://arxiv.org/abs/2009.01178), and the
[SATNet paper](https://arxiv.org/abs/1905.12149). The extended closest-work
discussion is explicitly pending; inherited uncited bibliography entries are
not claims that those comparisons have already been completed.

## Review and TODO locations

See [REVIEW.md](REVIEW.md) for the November 8 review of the argument, evidence,
and remaining gaps relative to an HPCA-style paper. Section VI-C has been removed.
Its tasks are retained as local TODOs:

| Task previously in VI-C | Current location |
| --- | --- |
| Workloads beyond Sudoku, including autonomous-driving tasks; define inputs, reusable constraints, and correctness | II-C: Hypotheses |
| Test H3 | II-C: Hypotheses |
| FIFO-depth and transfer-granularity sweeps | III-B: Hardware-Generated Delta |
| Fixed workload matrix, correctness checks, configuration order, budget, raw data, repeats, uncertainty | V-A: Configurations and Measurement Protocol |
| Controlled failure/boundary study | V-F: Correctness and Boundary Cases |
| Responses to midterm and milestone feedback | VII: Conclusion |

The review also adds TODOs for matched CPU/hardware payloads and schedules,
inference-path profiling, restoration pseudocode, and precise prior-work positioning.

## Before final submission

- For the next submission, evaluate a range of workloads beyond Sudoku, including
  autonomous-driving tasks that combine neural perception with symbolic reasoning.
- Replace interference-adjusted headline estimates with clean repeated runs;
  randomize configuration order and report uncertainty across runs.
- Test H3; complete controlled FIFO-depth and transfer-granularity sweeps.
- Add a controlled failure/boundary case and document hang/timeout handling.
- Quantify NVDLA/emulator logit differences and board ground-truth accuracy.
- Archive final routed resources, timing reports, source revisions, and binary hashes.
- Rebuild the Linux image with the layout fix and verify the packaged artifact.
- Complete closest-work comparisons, individual contributions, and feedback responses.
- Confirm the course's final-paper format and page limit, then remove resolved draft notes.

Red `Draft TODO` notes in the PDF deliberately identify unfinished evidence or
unconfirmed attribution. No new board runs or remote builds were performed to
prepare this draft.
