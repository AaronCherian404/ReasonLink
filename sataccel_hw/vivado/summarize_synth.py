"""Summarize the out-of-context synthesis of the SAT-Accel subsystem.

Reads <dir>/reports/utilization.rpt and timing_summary.rpt (vivado/synth_ooc.tcl)
and prints the main resources against the VCU118's xcvu9p plus the
post-synthesis worst negative slack.

Usage: python3 summarize_synth.py <build/vivado_bd> [out.md]
"""
import re
import sys
from pathlib import Path

# Row label in report_utilization -> short name
ROWS = [("CLB LUTs", "LUT"), ("CLB Registers", "FF"), ("Block RAM Tile", "BRAM36"),
        ("URAM", "URAM"), ("DSPs", "DSP")]


def util(text):
    out = {}
    for label, short in ROWS:
        m = re.search(r"^\|\s*" + re.escape(label) + r"\*?\s*\|\s*([\d.]+)\s*\|.*?\|\s*([\d.]+)\s*\|\s*([\d.<]+)\s*\|\s*$",
                      text, re.M)
        if m:
            out[short] = (m.group(1), m.group(2), m.group(3))
    return out


def wns(text):
    m = re.search(r"WNS\(ns\).*\n.*\n\s*([-\d.]+)\s+([-\d.]+)", text)
    return (m.group(1), m.group(2)) if m else ("?", "?")


def main():
    d = Path(sys.argv[1])
    u = util((d / "reports" / "utilization.rpt").read_text())
    w, t = wns((d / "reports" / "timing_summary.rpt").read_text())
    lines = ["| Resource | Used | Available (xcvu9p) | Util % |", "|---|---|---|---|"]
    for _, short in ROWS:
        if short in u:
            used, avail, pct = u[short]
            lines.append(f"| {short} | {used} | {avail} | {pct} |")
    lines.append("")
    lines.append(f"Post-synthesis timing at the 10 ns OOC clock: WNS {w} ns, TNS {t} ns.")
    out = "\n".join(lines)
    print(out)
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(out + "\n")


if __name__ == "__main__":
    main()
