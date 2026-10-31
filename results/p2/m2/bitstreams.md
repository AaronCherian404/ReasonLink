# VCU118 bitstreams with the Milestone 2 netlists (2026-10-31, torino, Vivado 2023.2)

SoC: ESP 2x3 (Ariane, NVDLA nv_small, SAT-Accel tile; 78.125 MHz tile clock
`mmcm_clkout1`). One synthesis of the SoC (SAT-Accel as a black box), two
implementation runs that link different SAT-Accel checkpoints
(`esp_soc/socs/reasonlink-vcu118/sataccel_impl.tcl`). Both include patch 0002
(`residentBase` = 0 behaves as the baseline), so each covers the full, delta
and ReasonLink handoffs with or without `--resident`.

| Bitstream | Run | SAT-Accel netlist | md5 |
|---|---|---|---|
| `top.bit` | impl_1 | SAT-Accel | 9a1e03be4bde279fcec516d4beed8234 |
| `top_rl.bit` | impl_rl | SAT-Accel + ReasonLink | 07b90e031c8c9d12f6a8cb6e89c512a6 |

## Timing (routed, after the clock fix; `<run>/sataccel_timing.rpt`)

| | impl_1 | impl_rl |
|---|---|---|
| WNS / TNS (ns) | -0.059 / -0.112 (4 endpoints) | **+0.036** / 0 |
| WHS (ns) | +0.003 | +0.009 |
| SoC tile clock `mmcm_clkout1`, intra | -0.059 (Ariane FPU cast -> issue operands) | +0.689 |
| SAT-Accel (`sataccel_clk`, 389,753 endpoints), intra | +2.546 | +2.681 |
| SAT-Accel -> SoC / SoC -> SAT-Accel | +7.201 / +10.377 | +7.144 / +10.066 |
| CLB LUTs (placed) | 340,859 (28.8%) | 344,875 (29.2%) |
| URAM | 434 (45.2%) | 434 (45.2%) |

impl_1's only violation is inside ESP's Ariane (4 endpoints, 59 ps at 12.8 ns),
not in SAT-Accel or its tile. impl_1's report predates the DDR group fix
below; its violations do not involve those clocks.

## Before the fix (as routed by ESP's runs)

WNS -4.110 ns (impl_1) and -4.403 ns (impl_rl), 6,872 and 8,125 failing
endpoints, almost all between `clk` and `mmcm_clkout1`/`mmcm_clkout0`.
`clk` is the 10 ns clock of SAT-Accel's out-of-context synthesis, which
`read_checkpoint -cell` applied to the net feeding the SAT-Accel cell in place
of the 12.8 ns tile clock. Both bitstream writes were then killed by memory
(two Vivado bitstream writes plus the SoC simulation at once, 02:20).

Fixes (`esp_soc/socs/reasonlink-vcu118/sataccel_clock.tcl`):
- `sataccel_fix_clock`: replace that clock by a generated copy of the tile
  clock on the same pin (Vivado: "completely overrides clock 'clk'") and make
  it asynchronous to the clocks ESP declares asynchronous to the tile clock.
- `sataccel_fix_ddr_groups`: ESP's XDC declares the two DDR controllers'
  clocks (`clkm`, `clkm1`) asynchronous, but no net named `clkm1` survives
  synthesis, so the group was never applied; impl_rl then failed by 0.555 ns
  on 3 DDR0 -> DDR1 reset-crossing endpoints. Declared again from each MIG's
  MMCM CLKOUT0.
- `sataccel_check_timing`: stop before the bitstream unless setup slack >=
  -`SATACCEL_MAX_VIOLATION` (0.1 ns for these) and hold slack >= 0.

The routed designs were kept; `sataccel_bit.tcl` (`make vivado-bit-rl`)
reopens each routed checkpoint the way the run does (its MIG calibration ELF
is loaded: "Memdata 28-144 Successfully populated the BRAM INIT strings"),
applies the fixes, checks timing and writes the bitstream.
