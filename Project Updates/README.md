# Project Updates

Course reports are organized by submission milestone. These folders are separate
from the two ReasonLink implementation milestones described in the P1 report.

```text
Project Updates/
├── Project Proposal/
│   ├── neural-symbolic-proposal.tex
│   ├── neural-symbolic-proposal.pdf
│   ├── figures/
│   ├── Template/                  original course template and instructions
│   └── build/                     LaTeX auxiliary files
├── P1 Exploration and Progress/
│   ├── neural-symbolic-p1.tex
│   ├── neural-symbolic-p1.pdf
│   ├── figures/                   diagrams, chart data, scripts, PDF/PNG exports
│   └── build/                     report and figure auxiliary files
├── P3 Evaluation Plan/
│   ├── neural-symbolic-p3.tex     P3 report (course P3 template)
│   ├── neural-symbolic-p3.pdf
│   └── figures/                   pipeline diagram, generated result chart and script
└── Final Paper/
    ├── neural-symbolic-final.tex  working final-paper draft
    ├── neural-symbolic-final.pdf  compiled draft
    ├── sections/                 editable paper sections
    ├── figures/                  diagrams, generated data, and generation script
    └── build/                    LaTeX auxiliary files
```

- [Project proposal](Project%20Proposal/neural-symbolic-proposal.pdf)
- [P1: Exploration and Progress](P1%20Exploration%20and%20Progress/neural-symbolic-p1.pdf)
- [P3: Evaluation Plan and Initial Results](P3%20Evaluation%20Plan/neural-symbolic-p3.pdf)
- [Final paper: working draft](Final%20Paper/neural-symbolic-final.pdf)

Each report folder includes its bibliography and the supplied `IEEEtran.cls`,
so it can be shared or uploaded to Overleaf independently. Keep its `figures/`
folder alongside the main `.tex` file. Select that report's named `.tex` file as
the main document in Overleaf.

## Build locally

From this directory, build the proposal with:

```sh
cd "Project Proposal"
latexmk -pdf neural-symbolic-proposal.tex
```

Or build P1 with:

```sh
cd "P1 Exploration and Progress"
latexmk -pdf neural-symbolic-p1.tex
```

Build the final-paper draft with:

```sh
cd "Final Paper"
latexmk -pdf neural-symbolic-final.tex
```

The local `.latexmkrc` files keep auxiliary files under `build/`; final PDFs
remain next to their sources. Previous build files were preserved under
`build/previous/` and `build/figures/previous/`. Build directories are ignored
by Git.

See each milestone's README for its contents and figure reproduction commands.
