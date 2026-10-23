"""Regenerate chart data from the recorded P1 software baseline summary."""

import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SUMMARY = REPO / "results" / "p1" / "baselines_summary.json"
OUTPUT = HERE / "baseline-walltime.tsv"


def main():
    summary = json.loads(SUMMARY.read_text())
    rows = ["group\tserial\tdouble"]
    for group, mode in enumerate(("faithful", "lean"), start=1):
        values = []
        for schedule in ("serial", "double"):
            run = summary[f"{schedule}_{mode}"]
            assert run["puzzles"] == 1000
            assert run["check"]["wrong"] == 0
            values.append(run["wall_us_per_puzzle"] / 1000)
        rows.append(f"{group}\t{values[0]:.12f}\t{values[1]:.12f}")
    OUTPUT.write_text("\n".join(rows) + "\n")
    print(f"Wrote {OUTPUT.name} from {SUMMARY.relative_to(REPO)}")


if __name__ == "__main__":
    main()
