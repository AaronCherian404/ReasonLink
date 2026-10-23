# Batch full-SoC simulation for `make sim` (ESP runs `vsim -c -do vsim.tcl`
# when this file exists in the SoC folder; install.sh links it there).
# Runs a bounded amount of simulated time and quits; the bare-metal test's
# UART output (incl. its PASS/FAIL line) appears in the transcript.
# Override the bound with SIM_TIME (e.g. SIM_TIME="200 ms").
onbreak {resume}
if {[info exists ::env(SIM_TIME)]} { set t $::env(SIM_TIME) } else { set t "60 ms" }
echo "ReasonLink: running $t of simulated time"
run $t
quit -f
