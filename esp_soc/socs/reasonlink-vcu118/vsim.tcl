# Batch full-SoC simulation for `make sim` (ESP runs `vsim -c -do vsim.tcl`
# when this file exists in the SoC folder; install.sh links it there).
# Runs a bounded amount of simulated time and quits; the bare-metal test's
# UART output (incl. its PASS/FAIL line) appears in the transcript.
# Override the bound with SIM_TIME (e.g. SIM_TIME="200 ms").
onbreak {resume}
if {[info exists ::env(SIM_TIME)]} { set t $::env(SIM_TIME) } else { set t "60 ms" }
# SATACCEL_AXI_VCD=<file>: also dump SAT-Accel's AXI master port, for
# sataccel_hw/tools/check_axi_vcd.py
if {[info exists ::env(SATACCEL_AXI_VCD)]} {
    set w /testbench/soc_inst/esp_1/tiles_gen(4)/accelerator_tile/tile_acc_i/tile_acc_1
    append w /CU_SATACCEL_gen/noc_CU_SATACCEL_i/CU_SATACCEL_rtl_i
    if {[llength [find instances $w]] == 0} {
        echo "ReasonLink: no instance $w"
        quit -f -code 1
    }
    vcd file $::env(SATACCEL_AXI_VCD)
    vcd add $w/clk $w/sataccel_ar* $w/sataccel_r* $w/sataccel_aw* $w/sataccel_w* $w/sataccel_b*
    echo "ReasonLink: dumping $w AXI to $::env(SATACCEL_AXI_VCD)"
}
echo "ReasonLink: running $t of simulated time"
run $t
quit -f
