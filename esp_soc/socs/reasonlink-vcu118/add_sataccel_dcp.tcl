# Add SAT-Accel's out-of-context checkpoint to ESP's Vivado project, so it
# fills the black box CU_SATACCEL/out/sataccel_top_sel.v declares for
# synthesis (see esp_soc/accelerator/CU_SATACCEL/Makefile). Run by the SoC
# Makefile's vivado-syn-rl target, from the vivado/ folder:
#   vivado -mode batch -source add_sataccel_dcp.tcl -tclargs <project.xpr> <sataccel_top.dcp>
lassign $argv xpr dcp
open_project $xpr
if {[llength [get_files -quiet [file tail $dcp]]] == 0} {
    add_files -norecurse $dcp
}
puts "ReasonLink: [get_files -quiet [file tail $dcp]] in [current_project]"
close_project
