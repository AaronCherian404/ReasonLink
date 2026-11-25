# P3: Evaluation Plan & Initial Results

Main source: `neural-symbolic-p3.tex`, following the course P3 template
(`coms6998-project-p3.zip`: same class and four sections, two pages excluding
references). Due Wednesday, November 25, 2026, 11:59 PM ET.

```sh
latexmk -pdf neural-symbolic-p3.tex
# or: pdflatex, bibtex, pdflatex, pdflatex
```

For Overleaf, upload this whole folder and select `neural-symbolic-p3.tex`.
`IEEEtran.bst` and `flushend.sty` come with Overleaf; the file builds without
`flushend` where it is missing.

## Figures

- `figures/pipeline-diagram.png`: the handoff-path diagram from the November 6
  midterm presentation (`A8_Presentation.pptx`, slide 3).
- `figures/p3-results.tex` and `figures/p3-macros.tex`: generated from the board
  summary by `python3 figures/generate-p3-figures.py`, which reads
  `../../results/p2/board/csv/board_summary.json` (standard library only, runs no
  experiments).

## Evidence

All numbers come from the 2026-11-05 board runs (`results/p2/board/`), the
functional and RTL logs in `results/p2/m2/`, and `PROGRESS.md`. The NVDLA driver
finding (ESP builds `opendla.ko` with `VERBOSE=1`) comes from the driver source and
disassembly on torino. Its effect on board timing has **not been measured yet**
(the first Milestone 4 run: `board/ops/nvprof_run.sh`).

