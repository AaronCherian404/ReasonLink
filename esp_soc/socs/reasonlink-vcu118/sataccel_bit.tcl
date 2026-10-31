# Write a routed run's bitstream with SAT-Accel's clock fixed and timing
# checked (sataccel_clock.tcl), for runs routed before the link hooks fixed
# the clock: placement and routing stand, only the timing constraints were
# wrong. Vivado will not resume such a run at write_bitstream once its
# properties have changed ("need to be reset"), so this opens the routed
# checkpoint the way the run itself opens its design (the setup lines of the
# run's top.tcl: in-memory project, board part, the MIG and SGMII IP, whose
# ELF the bitstream needs, and link_design), then writes top.bit in the run
# folder.
#
#   cd vivado/<design>.runs/<run> && vivado -mode batch -source sataccel_bit.tcl
set here [info script]
if {[file type $here] eq "link"} { set here [file join [file dirname $here] [file readlink $here]] }
source [file join [file dirname [file normalize $here]] sataccel_clock.tcl]

if {![file exists top_routed.dcp]} { error "ReasonLink: no top_routed.dcp in [pwd]" }
set f [open top.tcl]
set setup [split [read $f] "\n"]
close $f
foreach l $setup {
    set l [string trim $l]
    if {[regexp {^(create_project -in_memory|set_property (board_part|design_mode|XPM_LIBRARIES) |read_ip )} $l]} {
        eval $l
    }
}
add_files top_routed.dcp
foreach l $setup {
    if {[regexp {^\s*link_design } $l]} { eval [string trim $l] }
}

sataccel_fix_clock [sataccel_cell]
sataccel_fix_ddr_groups
sataccel_check_timing sataccel_timing.rpt
# as in the link hooks (sataccel_impl.tcl): linking re-applies ESP's
# set_property ODT/OUTPUT_IMPEDANCE {} on the LVDS SGMII pins (PORTPROP-6)
reset_property -quiet ODT [get_ports -quiet {rxp rxn}]
reset_property -quiet OUTPUT_IMPEDANCE [get_ports -quiet {txp txn}]
write_mem_info -force -no_partial_mmi top.mmi
write_bitstream -force top.bit
puts "ReasonLink: wrote [file normalize top.bit]"
