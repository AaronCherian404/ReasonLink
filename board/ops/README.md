# Board operation scripts (torino, `~/m2/`)

Used for the 2026-11-05 board run. Copy them to `~/m2/` on torino; the board's
root password comes from the `RL_PW` environment variable and is never stored.

| Script | What it does |
|---|---|
| `bssh.sh`, `askpass.sh` | SSH to the board's root shell through espgate:5508 |
| `boot_and_run.sh <batch>` | Reprogram the FPGA with `top_rl.bit` (a hung accelerator survives `esplink --reset`), boot Linux, copy `rl_batch.sh` and `rl_board_fix` onto the board, start one batch |
| `boot_retry.sh` | `make fpga-run-linux` until the EDCL load succeeds; one more `esplink --reset` if no kernel starts; waits for the login prompt (needs the UART logger) |
| `rl_batch.sh <batch>` | On the board: bring-up checks (stop if either accelerator is unhealthy), then the batch's `rl_board` configurations, 100 puzzles each |
| `board_fetch.sh` | Every 5 min: copy `results/*.csv` and `/root/rl_*.log` off the board (no tar there: text between markers) |
| `board_put.sh`, `board_put_bin.sh` | Copy a text file or a binary (base64, 8 KB chunks) onto the board |

UART logger (tmux window `uart`): `while true; do socat -u TCP:espdev:4331 - >> ~/m2/uart_live.log; sleep 2; done`.
`rl_board_fix` is `board/rl_board` with the ReasonLink layout fix, built in `~/m2/rldbg` with the
Makefile's flags against the existing objects until the Linux image is rebuilt.
