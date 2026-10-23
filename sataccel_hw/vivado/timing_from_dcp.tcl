# Post-synthesis timing of the OOC checkpoint at a given clock period
# (for checkpoints synthesized without the clock constraint applied).
# vivado -mode batch -source timing_from_dcp.tcl -tclargs <dcp> <period_ns> <report>
lassign $argv dcp period rpt
open_checkpoint $dcp
create_clock -name clk -period $period [get_ports clk]
report_timing_summary -max_paths 20 -file $rpt
