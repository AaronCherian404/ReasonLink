# Out-of-context synthesis of the SAT-Accel subsystem (rtl/sataccel_top.v +
# block design), for ESP's third-party accelerator flow, which takes only
# Verilog/SV/VHDL file lists (no .xci): the HLS floating-point cores and
# SmartConnects are flattened into one structural netlist of Xilinx
# primitives. ESP's Vivado flow re-synthesizes it; its ModelSim flow
# simulates it with unisims (compile_simlib), which ESP already compiles.
#
# Outputs (in the current directory, build/vivado_bd):
#   sataccel_top_funcsim.v    netlist for ESP (+ sim)
#   sataccel_top_stub.v       port declaration
#   sataccel_top.dcp          checkpoint
#   reports/                  utilization (hierarchical), timing
#
# Run from build/vivado_bd: vivado -mode batch -source ../../vivado/synth_ooc.tcl -tclargs <sataccel_hw dir> <period_ns> <jobs>
set root   [lindex $argv 0]
set period [lindex $argv 1]
set jobs   [lindex $argv 2]

open_project sataccel_bd.xpr
if {[llength [get_files -quiet sataccel_top.v]] == 0} {
    add_files -norecurse [glob $root/rtl/*.v]
}
set_property top sataccel_top [current_fileset]

# Clock constraint for out-of-context synthesis
set xdc [file normalize ./sataccel_ooc.xdc]
set f [open $xdc w]
puts $f "create_clock -name clk -period $period \[get_ports clk\]"
close $f
if {[llength [get_files -quiet sataccel_ooc.xdc]] == 0} {
    add_files -fileset constrs_1 -norecurse $xdc
    set_property USED_IN {synthesis out_of_context} [get_files sataccel_ooc.xdc]
}

set_property -name {STEPS.SYNTH_DESIGN.ARGS.MORE OPTIONS} -value {-mode out_of_context} -objects [get_runs synth_1]
reset_run synth_1
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] != "100%"} { error "synth_1 failed" }

open_run synth_1 -name synth_1
file mkdir reports
report_utilization -file reports/utilization.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file reports/utilization_hier.rpt
report_timing_summary -max_paths 20 -file reports/timing_summary.rpt
write_checkpoint -force sataccel_top.dcp
write_verilog -mode funcsim -force sataccel_top_funcsim.v
write_verilog -mode synth_stub -force sataccel_top_stub.v
close_project
