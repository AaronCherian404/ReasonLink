# Synthesize one SAT-Accel kernel to Verilog with Vitis HLS alone (no v++):
# the same per-kernel compile SAT-Accel's runCompile.sh does with `v++ -c`
# (Vitis kernel flow, -DFPGA_HW), exported as a Vivado IP for our own
# top level (Stage 3) instead of an .xo for the Vitis linker.
#
# vitis_hls -f hls_kernel.tcl -tclargs <top> <out_dir> <period_ns> <part> <src_dir> <file>...
# vitis_hls passes its own arguments in $argv too: keep what follows -tclargs
# (or this script's path).
set args $argv
set i [lsearch -exact $args -tclargs]
if {$i < 0} { set i [lsearch -glob $args *hls_kernel.tcl] }
if {$i >= 0} { set args [lrange $args [expr {$i + 1}] end] }
set top    [lindex $args 0]
set out    [lindex $args 1]
set period [lindex $args 2]
set part   [lindex $args 3]
set src    [lindex $args 4]
set files  [lrange $args 5 end]

cd $out
open_project -reset $top
set_top $top
# ap_utils.h (ap_wait) lives in include/etc in Vitis HLS 2023.2
set etc $::env(XILINX_HLS)/include/etc
foreach f $files {
    add_files $src/$f -cflags "-DFPGA_HW -I$src -I$etc -std=c++14"
}
open_solution -reset sol -flow_target vitis
set_part $part
create_clock -period $period -name default
config_export -format ip_catalog -rtl verilog -ipname $top -vendor columbia.edu -library reasonlink -version 1.0

csynth_design
export_design -format ip_catalog -rtl verilog
exit
