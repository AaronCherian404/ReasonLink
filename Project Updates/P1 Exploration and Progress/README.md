# P1: Exploration and Progress

The main document is `neural-symbolic-p1.tex`; its compiled PDF has two pages of
main content and one page of references.

```sh
latexmk -pdf neural-symbolic-p1.tex
```

## Contents

- `neural-symbolic-references.bib` and `p1-additional-references.bib`: cited sources.
- `IEEEtran.cls`: supplied conference class.
- `figures/reasonlink-baseline-paper.tex`: compact pipeline diagram used in the report.
- `figures/reasonlink-baseline-comparison.tex`: detailed pipeline diagram.
- `figures/baseline-walltime-chart.tex`: grouped software baseline chart.
- `figures/baseline-walltime.tsv`: chart data in milliseconds per puzzle.
- `figures/*-figure.tex`: standalone figure wrappers, with PDF and PNG exports.
- `build/`: report and figure auxiliary files, including preserved previous builds.

## Reproduce figures

Regenerate chart data from the repository's `results/p1/baselines_summary.json`:

```sh
python3 figures/generate-baseline-walltime.py
```

Compile the standalone figures from `figures/`:

```sh
cd figures
latexmk -pdf baseline-walltime-figure.tex
latexmk -pdf reasonlink-baseline-paper-figure.tex
latexmk -pdf reasonlink-baseline-figure.tex
```

PDF exports are vector graphics. PNG previews can be regenerated with Poppler:

```sh
pdftoppm -scale-to 1600 -png -singlefile baseline-walltime-figure.pdf baseline-walltime-figure
pdftoppm -scale-to 2200 -png -singlefile reasonlink-baseline-paper-figure.pdf reasonlink-baseline-paper-figure
pdftoppm -scale-to 2200 -png -singlefile reasonlink-baseline-figure.pdf reasonlink-baseline-figure
```

To upload the report to Overleaf, include this entire milestone folder and select
`neural-symbolic-p1.tex` as the main document. The recorded chart data is included;
regenerating it requires the full ReasonLink repository.
