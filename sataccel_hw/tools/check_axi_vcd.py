"""Check SAT-Accel's AXI traffic in a full-SoC simulation against its image.

vsim.tcl (SATACCEL_AXI_VCD=<file>) dumps the SAT-Accel tile's AXI master
port. This replays it on a memory model that starts from the bare-metal
test's image (sataccel_test.h) and takes every write the accelerator makes,
and reports read beats whose data differ from the model (data the
accelerator should have seen), reads outside the image, error responses,
and the result words (miscCounters, answerStack) the accelerator wrote.
A pause of more than --gap-us in the traffic starts a new run (the test
reloads the image between its runs).

Usage: python3 check_axi_vcd.py <axi.vcd> <sataccel_test.h> [--gap-us N] [--dump-prefix P]
  --dump-prefix P: write each run's final memory model to P<run>.txt
"""
import argparse
import re
import sys


def parse_header(path):
    s = open(path).read()
    runs = [(int(a, 16), int(n)) for a, n in
            re.findall(r"\{0x([0-9a-f]+), (\d+)\}", s.split("sa_test_runs")[1].split("};")[0])]
    data = [int(v, 16) for v in re.findall(r"0x([0-9a-f]+)ull", s.split("sa_test_data")[1].split("};")[0])]
    image, k = {}, 0
    for a, n in runs:
        for i in range(n):
            image[(a >> 3) + i] = data[k]
            k += 1
    assert k == len(data), (k, len(data))
    misc = int(re.search(r"SA_TEST_MISC_ADDR 0x([0-9a-f]+)", s).group(1), 16)
    ans = int(re.search(r"SA_TEST_ANSWER_ADDR 0x([0-9a-f]+)", s).group(1), 16)
    return image, misc, ans


def vcd_cycles(path):
    """Yield (time, values) at each rising edge of clk, values as before the edge.

    values: port name without the 'sataccel_' prefix -> int, or None if x/z.
    Vectors may be dumped whole or bit by bit."""
    ids = {}                     # VCD id -> [(name, bit or None)]
    bits = {}                    # name -> {bit: id} for bit-by-bit vectors
    with open(path) as f:
        for line in f:
            if line.startswith("$var"):
                t = line.split()
                m = re.match(r"(\S+?)\s*(?:\[(\d+)(:\d+)?\])?$", " ".join(t[4:-1]))
                name = m.group(1).split(".")[-1].replace("sataccel_", "")
                bit = int(m.group(2)) if m.group(2) is not None and m.group(3) is None else None
                ids.setdefault(t[3], []).append((name, bit))
                if bit is not None:
                    bits.setdefault(name, {})[bit] = t[3]
            elif line.startswith("$enddefinitions"):
                break
        whole = {n: i for i, lst in ids.items() for n, b in lst if b is None}
        names = sorted(set(whole) | set(bits))
        clk_id = whole["clk"]
        val = {}                 # id -> bit string

        def value(name):
            if name in bits:
                s = "".join(val.get(bits[name][b], "x") for b in sorted(bits[name], reverse=True))
            else:
                s = val.get(whole[name], "x")
            return None if re.search("[xzXZ]", s) else int(s, 2)

        now, pending = 0, []
        for line in f:
            c = line[0]
            if c == "#" or c == "$":
                if pending:
                    if val.get(clk_id) == "0" and any(i == clk_id and v == "1" for i, v in pending):
                        yield now, {n: value(n) for n in names}
                    val.update(pending)
                    pending = []
                if c == "#":
                    now = int(line[1:])
            elif c in "01xzXZ":
                pending.append((line[1:].strip(), c))
            elif c in "bB":
                v, i = line[1:].split()
                pending.append((i, v))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vcd")
    ap.add_argument("header")
    ap.add_argument("--gap-us", type=float, default=50.0)
    ap.add_argument("--dump-prefix")
    a = ap.parse_args()
    image, misc_addr, ans_addr = parse_header(a.header)
    gap = int(a.gap_us * 1e9)    # vsim -t fs

    runs = []
    mem = None
    last = None
    rq, wq, wbeats = [], [], []
    for t, v in vcd_cycles(a.vcd):
        hs = {ch: v.get(ch + "valid") == 1 and v.get(ch + "ready") == 1 for ch in ("ar", "r", "aw", "w", "b")}
        if not any(hs.values()):
            continue
        if last is None or t - last > gap:
            mem = dict(image)
            st = {"start": t, "ar": 0, "r": 0, "aw": 0, "w": 0, "b": 0, "bad": [], "outside": [],
                  "xdata": 0, "rresp": 0, "bresp": 0, "mem": mem, "writes": {},
                  "partial": 0, "nostrb": dict(image), "result_beats": []}
            runs.append(st)
            rq, wq, wbeats = [], [], []
        last = t
        st["end"] = t
        if hs["ar"]:
            st["ar"] += 1
            rq.append([v["araddr"], v["arlen"], v["arsize"], 0])
        if hs["r"]:
            st["r"] += 1
            if not rq:
                st["bad"].append((t, None, None, v["rdata"], "R beat without AR"))
            else:
                ad, ln, sz, k = rq[0]
                ba = ((ad >> sz) << sz) + (k << sz) if k else ad
                w = ba >> 3
                d = v["rdata"]
                if v["rresp"]:
                    st["rresp"] += 1
                if d is None:
                    st["xdata"] += 1
                elif w not in mem:
                    st["outside"].append((t, ba, d))
                else:
                    lo, nb = (ba & 7), (1 << sz)
                    mask = ((1 << (8 * nb)) - 1) << (8 * lo) if nb < 8 else (1 << 64) - 1
                    if (d & mask) != (mem[w] & mask):
                        st["bad"].append((t, ba, mem[w], d, ""))
                rq[0][3] += 1
                if v["rlast"] == 1 or rq[0][3] > ln:
                    rq.pop(0)
        if hs["aw"]:
            st["aw"] += 1
            wq.append([v["awaddr"], v["awlen"], v["awsize"], 0])
        if hs["w"]:
            st["w"] += 1
            wbeats.append((v["wdata"], v["wstrb"], v["wlast"]))
        while wq and wbeats:
            d, sb, wl = wbeats.pop(0)
            ad, ln, sz, k = wq[0]
            ba = ((ad >> sz) << sz) + (k << sz) if k else ad
            w = ba >> 3
            old = mem.get(w, 0)
            if d is None or sb is None:
                st["xdata"] += 1
            else:
                for b in range(8):
                    if sb >> b & 1:
                        old = (old & ~(0xFF << 8 * b)) | (d & (0xFF << 8 * b))
                mem[w] = old
                st["writes"][w] = old
                st["nostrb"][w] = d          # the same beat if the strobes were ignored
                if sb != 0xFF:
                    st["partial"] += 1
                if any(lo <= ba < lo + 64 for lo in (misc_addr, ans_addr)):
                    st["result_beats"].append((ba, sz, sb, d))
            wq[0][3] += 1
            if wl == 1 or wq[0][3] > ln:
                wq.pop(0)
        if hs["b"]:
            st["b"] += 1
            if v["bresp"]:
                st["bresp"] += 1

    def i32(m, addr, i):
        w = m.get((addr >> 3) + (i >> 1), 0)
        x = (w >> (32 * (i & 1))) & 0xFFFFFFFF
        return x - (1 << 32) if x >> 31 else x

    if not runs:
        print("no AXI traffic in", a.vcd)
        return 1
    for n, st in enumerate(runs):
        m = st["mem"]
        print("run %d: %.3f-%.3f ms: AR %d, R %d, AW %d, W %d, B %d; error resp R %d B %d; x data %d"
              % (n, st["start"] / 1e12, st["end"] / 1e12, st["ar"], st["r"], st["aw"], st["w"], st["b"],
                 st["rresp"], st["bresp"], st["xdata"]))
        print("  read beats differing from the model: %d; outside the image: %d" % (len(st["bad"]), len(st["outside"])))
        for t, ba, exp, got, why in st["bad"][:20]:
            print("    %.6f ms  %s  expect %s  got %s %s" % (t / 1e12, "%08x" % ba if ba is not None else "-",
                  "%016x" % exp if exp is not None else "-", "%016x" % got if got is not None else "x", why))
        for t, ba, got in st["outside"][:10]:
            print("    %.6f ms  %08x  (outside image)  got %016x" % (t / 1e12, ba, got))
        print("  written words: %d; misc %s; answer %s" % (
            len(st["writes"]), [i32(m, misc_addr, i) for i in range(8)], [i32(m, ans_addr, i) for i in range(8)]))
        ns = st["nostrb"]
        print("  write beats with partial strobes: %d; if the strobes were ignored: misc %s; answer %s" % (
            st["partial"], [i32(ns, misc_addr, i) for i in range(8)], [i32(ns, ans_addr, i) for i in range(8)]))
        for ba, sz, sb, d in st["result_beats"]:
            print("    result write %08x size %d strb %02x data %016x" % (ba, 1 << sz, sb, d))
        if a.dump_prefix:
            with open("%s%d.txt" % (a.dump_prefix, n), "w") as f:
                for w in sorted(m):
                    f.write("%08x %016x\n" % (w << 3, m[w]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
