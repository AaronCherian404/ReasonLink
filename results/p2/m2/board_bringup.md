# Board bring-up on the VCU118 (2026-11-04/05, `top_rl.bit`)

Bitstream `top_rl.bit` (md5 192946a19dbca44683fa6ecf6228f958, `wstrb_fix.md`),
programmed with `vivado/program.tcl` on impl_rl's bit. Linux: `make fpga-run-linux`.

## What works

| Check | Result |
|---|---|
| Bare-metal quinn (`sataccel.exe`, coherent DMA) | **PASS**: SAT, 5 decisions, 1 conflict; misc = [6, 5, 0, 1, 0, 16, 1, 2], as in RTL and the full-SoC simulation. The write-strobe fix works in hardware |
| NVDLA, ESP's `nvdla_runtime` on seven.pgm | `0 2 0 0 0 0 0 124 0 0`, ESP's published output (coherent DMA) |
| SAT-Accel under Linux, `sataccel_run` base.cnf | PASS, SAT; one interrupt per solve; solve 951k cycles at 78.1 MHz (tile busy 867k) |
| `rl_board` (NVDLA + SAT-Accel, serial, faithful), 2 puzzles | 2/2 grids equal the reference |

## Bugs found on the board (all in our software, all fixed)

1. **Blocked interrupt after the bare-metal test.** The test replays the
   driver's register program, which enables the solver interrupt (IER/GIE).
   ESP's socket sends a level interrupt once, then waits for the IO tile's ack,
   which the IO tile sends only when the CPU writes PLIC complete for that source
   (`noc_CU_SATACCEL.vhd` `wait_for_clear_irq`, `tile_io.vhd` `fsm_intr_ack`).
   With no handler, the ack never came; resetting the CPU for Linux clears the
   PLIC, not the socket. Linux then saw no SAT-Accel interrupt (`/proc/interrupts`
   0), and every solve waited out the 60 s timeout before polling. (An earlier
   manual ack went to 0x0C201004; the PLIC is at 0x6C000000.) Fix: the bare-metal
   test skips IER/GIE; the UMD polls after a 1 s wait (`SATACCEL_WAIT_MS`, 0 =
   poll only); `run_baselines.sh` checks that the interrupt arrives.
2. **Interrupt storm in the kernel ISR.** The ISR wrote 1 to the solver's
   ISR register, which toggles on write. ESP's socket re-sends the interrupt if the
   line is still high when the ack arrives, and the posted write can land after
   it. The repeated ISR then set the bit again, and the CPU hung on the first
   solve with interrupts working. Fix: the ISR clears GIE (idempotent) and
   reads it back. The driver core clears stale status before enabling.
3. **SAT-Accel's DMA buffer on top of NVDLA's memory.** Both device-tree nodes
   use `thirdparty_reserved` (0xa0200000, 510 MB). NVDLA's driver declares its
   own pool over it (`dma_declare_coherent_memory`); `sataccel.ko`'s
   `of_reserved_mem_device_init` creates a second one. Both handed out
   0xa0200000. `rl_board` opens NVDLA, then zeroes SAT-Accel's buffers, wiping
   NVDLA's task descriptors: the firmware read `num_operations == 0` (NULL
   tasks, wrong logits), then a submit hung. Fix: `sataccel.ko` first takes a
   256 MB guard block (`guard_size`), so its buffer is at 0xb0200000.

4. **ReasonLink read channels 8, 9 from the wrong surface** (found in the
   board run, 2026-11-05). NVDLA's ip2 output on the board is a 1x1x10 tensor
   in 8-channel groups with an **8 B surface stride** (`RL_DEBUG` dump:
   `stride=[1 8 8 0]`, channel 8 at byte 8). ReasonLink's LAYOUT register holds
   the surface stride in units of 256 B, and `sa_rl_setup` rounded 8 down to 0,
   so the second read fetched channels 0, 1 again: every given 8 and 9 decoded
   to another digit (38 of 38 wrong literals on puzzles 0-5 were 8s and 9s).
   Every solve ended UNSAT; the "19/100 match" were the 19 puzzles whose
   reference answer is UNSAT anyway (misclassified givens), so all 81
   satisfiable puzzles were wrong. The RTL test used the contiguous layout (`tb_tool`,
   group 32), so simulation never ran this path. Fix (driver only, no new
   bitstream): with an 8 B stride the surfaces are adjacent, so `sa_rl_setup`
   programs the contiguous layout; any stride the register cannot hold is now
   an error. After the fix: 0 wrong literals on puzzles 0-5
   (`results/p2/board/rl_debug_layoutbug.log` has the dump before the fix).

## Board operation (2026-11-05)

- **A hung accelerator survives a soft reset.** `esplink --reset` resets the
  processor cores only. After the double-buffered lean run hung (NVDLA stalled
  mid-task: no interrupt pending in NVDLA or the PLIC, `rl_board` in
  `nvdla_task_submit`), two soft reboots kept failing: the next `rl_board`
  hung on its first task, then NVDLA's firmware failed
  `conv.c:346 (reg == 0)` and SAT-Accel returned ERROR on base.cnf.
  Reprogramming the FPGA cleared both. The boot script
  (`~/m2/boot_and_run.sh`) now always reprograms, and the batch script stops
  if the bring-up checks fail.
- **NVDLA hangs mid-task**, twice on 11-05: double-buffered lean (about
  puzzle 75, after 300+ good puzzles in the same boot) and serial ReasonLink
  + resident (about puzzle 15). The previous night's unexplained loss of SSH
  was probably the same (the board degrades over ~45 min after a hang:
  `dmesg`, then SSH, stop answering). Both were in runs where the SAT-Accel
  tile does DMA while NVDLA runs, and both while the board was being polled
  over SSH; a later batch of 500 such puzzles with no SSH during the runs did
  not hang. Cause not found.
- **SSH perturbs measurements.** A login costs the 78 MHz CPU about 1-1.5 s,
  which lands in the running `rl_board` (0.3-3 s stalls in single puzzles);
  measured runs go detached with no SSH until they end.
- **EDCL is unreliable.** `fpga-run-linux`'s DRAM load often fails ("Handle
  EDCL message failed after 10 attempts"); retrying works (1-7 attempts). Once
  the final soft reset after a good load was lost (no boot banner); one more
  `esplink --reset` booted it. `~/m2/boot_retry.sh` retries both.
- **Console:** `socat -u TCP:espdev:4331 -` logs the UART to a file without
  minicom (espdev drops idle connections every ~10 min; the logger reconnects).
- **No tar/gzip on the board:** results are copied off as text (CSV between
  markers) and binaries pushed as base64 in 8 KB chunks (dropbear refuses
  longer commands).

## Other findings

- ESP's NVDLA driver logs about 300 debug lines per task. On the 115200-baud
  console that is about 4 s per inference, against about 0.24 s with
  `dmesg -n 1` (in `run_baselines.sh`, which also stops klogd).
- NVDLA needs coherent DMA. In ESP's default mode (0), seven.pgm gave
  `0 3 0 2 0 0 0 120 0 0` and `0 11 0 1 0 0 0 113 0 0` in 2 of 3 runs (stale
  reads). `sataccel.ko` sets NVDLA's tile to `ACC_COH_RECALL` as well.
- NVDLA vs the int8 emulator: 21 of 61 cells differ in the logits, but every
  argmax is right (both grids correct).
- UART login fails (input path); SSH through espgate:5508 works.

## Reproduce

torino: `~/m2_linux2.sh` (bare-metal test, `make CU_SATACCEL`, `make linux`,
with checks), then from `~/esp/socs/reasonlink-vcu118`:
`cd vivado && vivado -mode batch -source program.tcl -tclargs espdev 3132 xcvu9p $PWD/esp-xilinx-vcu118-xcvu9p.runs/impl_rl/top.bit`,
then `make fpga-run-linux`. On the board (`ssh -p 5508 root@espgate`):
`sh /root/CU_SATACCEL/rl/run_baselines.sh 100`.
`linux.bin` with all three fixes: md5 bd35ffcbee979a0dbea3b023fb461e72.
