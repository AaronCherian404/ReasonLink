"""Summarize Vitis HLS results for the seven SAT-Accel kernels.

Reads <hls_dir>/<kernel>/sol/syn/report/csynth.xml and prints, per kernel,
the estimated clock and the resource estimates, plus totals against the
VCU118's xcvu9p. HLS estimates only; Vivado synthesis (Stage 3) is the
authoritative count.

Usage: python3 summarize_hls.py <hls_dir> [out.md]
"""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

KERNELS = ["solver", "clause_store_handler", "location_handler", "restartCalculator",
           "timer", "pqHandler", "message"]
RES = ["LUT", "FF", "BRAM_18K", "URAM", "DSP"]
XCVU9P = {"LUT": 1182240, "FF": 2364480, "BRAM_18K": 4320, "URAM": 960, "DSP": 6840}


def kernel_row(hls_dir, k):
    rpt = Path(hls_dir) / k / "sol" / "syn" / "report" / "csynth.xml"
    root = ET.parse(rpt).getroot()
    timing = root.find("PerformanceEstimates/SummaryOfTimingAnalysis")
    est = float(timing.findtext("EstimatedClockPeriod"))
    target = float(root.findtext("UserAssignments/TargetClockPeriod"))
    res = root.find("AreaEstimates/Resources")
    vals = {r: int(res.findtext(r) or 0) for r in RES}
    return target, est, vals


def main():
    hls_dir = sys.argv[1]
    lines = ["| Kernel | Target / est. clock (ns) | " + " | ".join(RES) + " |",
             "|---|---|" + "---|" * len(RES)]
    total = {r: 0 for r in RES}
    for k in KERNELS:
        target, est, vals = kernel_row(hls_dir, k)
        for r in RES:
            total[r] += vals[r]
        lines.append(f"| {k} | {target:g} / {est:.2f} | " + " | ".join(f"{vals[r]:,}" for r in RES) + " |")
    lines.append("| **total** | | " + " | ".join(f"**{total[r]:,}**" for r in RES) + " |")
    lines.append("| xcvu9p (VCU118) | | " + " | ".join(
        f"{XCVU9P[r]:,} ({100 * total[r] / XCVU9P[r]:.0f}%)" for r in RES) + " |")
    out = "\n".join(lines)
    print(out)
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(out + "\n")


if __name__ == "__main__":
    main()
