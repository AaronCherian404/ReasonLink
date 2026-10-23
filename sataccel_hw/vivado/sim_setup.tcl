# Add our RTL + testbench to the block-design project and prepare an xsim
# snapshot of tb/tb_sataccel.sv (compile + elaborate). Tests then run with
# tools/run_rtl_tests.sh, which calls xsim on the snapshot with plusargs.
#
# Run from build/vivado_bd: vivado -mode batch -source ../../vivado/sim_setup.tcl -tclargs <sataccel_hw dir>
set root [lindex $argv 0]
open_project sataccel_bd.xpr
add_files -norecurse [glob $root/rtl/*.v]
add_files -fileset sim_1 -norecurse [glob $root/tb/*.sv]
set_property top sataccel_top [current_fileset]
set_property top tb_sataccel [get_filesets sim_1]
set_property top_lib xil_defaultlib [get_filesets sim_1]
set_property -name {xsim.elaborate.debug_level} -value {off} -objects [get_filesets sim_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
launch_simulation -scripts_only -simset sim_1 -mode behavioral
close_project
