# Build the SAT-Accel subsystem as a Vivado block design: what Vitis's
# `v++ -l --config k2k.cfg` would link, without Vitis.
#   - the seven HLS kernel IPs (../build/hls/<kernel>/sol/impl/ip)
#   - AXI-Stream connections exactly as k2k.cfg's stream_connect lines
#     (messageStream through a depth-16 FIFO, as ":16" requests)
#   - SmartConnect: the kernels' m_axi masters -> one 64-bit AXI4 master
#     (ESP's third-party socket: one 64-bit master, 32-bit addresses)
#   - SmartConnect: one AXI4-Lite slave -> the kernels' control ports,
#     64 KB per kernel (see KERNEL_BASE below and sataccel_regs.h)
#   - with REASONLINK=1: rtl/reasonlink_core.v as one more cell (memory
#     master on mem_sc, control window at 0x70000)
#
# vivado -mode batch -source sataccel_bd.tcl -tclargs <hls_dir> <out_dir> <part> <period_ns> [reasonlink 0|1] [rtl_dir]
set hls_dir [lindex $argv 0]
set out_dir [lindex $argv 1]
set part    [lindex $argv 2]
set period  [lindex $argv 3]
set reasonlink [expr {[llength $argv] > 4 ? [lindex $argv 4] : 0}]
set rtl_dir [lindex $argv 5]
set freq_hz [expr {int(1000.0 / $period * 1e6)}]

create_project -force sataccel_bd $out_dir -part $part
set repos {}
foreach d [glob $hls_dir/*/sol/impl/ip] { lappend repos $d }
set_property ip_repo_paths $repos [current_project]
update_ip_catalog

create_bd_design sataccel_bd

# Kernels, in k2k.cfg's instance naming (<kernel>_1). Control windows:
array set KERNEL_BASE {
    message              0x00000
    clause_store_handler 0x10000
    location_handler     0x20000
    timer                0x30000
    restartCalculator    0x40000
    pqHandler            0x50000
    solver               0x60000
    reasonlink           0x70000
}
set kernels {message clause_store_handler location_handler timer restartCalculator pqHandler solver}
foreach k $kernels {
    create_bd_cell -type ip -vlnv columbia.edu:reasonlink:$k:1.0 ${k}_1
}
# Control-window owners: the kernels, plus ReasonLink when enabled
set ctrl_cells $kernels
if {$reasonlink} {
    add_files -norecurse $rtl_dir/reasonlink_core.v
    update_compile_order -fileset sources_1
    create_bd_cell -type module -reference reasonlink_core reasonlink_1
    lappend ctrl_cells reasonlink
}
proc ctrl_pin {k} {
    if {$k eq "reasonlink"} { return [get_bd_intf_pins reasonlink_1/s_axi] }
    return [get_bd_intf_pins ${k}_1/s_axi_control]
}

# Clock and reset
create_bd_port -dir I -type clk -freq_hz $freq_hz ap_clk
create_bd_port -dir I -type rst ap_rst_n
set_property CONFIG.POLARITY ACTIVE_LOW [get_bd_ports ap_rst_n]
set_property CONFIG.ASSOCIATED_RESET ap_rst_n [get_bd_ports ap_clk]

# AXI-Stream: an HLS port named after a Verilog keyword or a reserved word
# gets an "_r" suffix (e.g. timer's value -> value_r).
proc kpin {inst port} {
    foreach p [list $port ${port}_r] {
        set pin [get_bd_intf_pins -quiet $inst/$p]
        if {$pin ne ""} { return $pin }
    }
    error "no interface $inst/$port"
}
# k2k.cfg stream_connect, src -> dst
set streams {
    {clause_store_handler_1 clauseStoreOutputStream1 solver_1 clauseStoreOutputStream1}
    {clause_store_handler_1 clauseStoreOutputStream2 solver_1 clauseStoreOutputStream2}
    {solver_1 clauseStoreInputStream1 clause_store_handler_1 clauseStoreInputStream1}
    {solver_1 clauseStoreInputStream2 clause_store_handler_1 clauseStoreInputStream2}
    {clause_store_handler_1 locationInputStream location_handler_1 locationInputStream}
    {location_handler_1 locationOutputStream solver_1 locationOutputStream}
    {restartCalculator_1 value solver_1 restartValueStream}
    {solver_1 stopStream restartCalculator_1 stop}
    {timer_1 value solver_1 timerValueStream}
    {solver_1 conditionStream timer_1 condition}
    {pqHandler_1 output solver_1 pqHandlerValue}
    {solver_1 pqHandlerInput pqHandler_1 input}
}
foreach s $streams {
    lassign $s si sp di dp
    connect_bd_intf_net [kpin $si $sp] [kpin $di $dp]
}
# solver_1.messageStream -> message_1.messageStream, depth 16
create_bd_cell -type ip -vlnv xilinx.com:ip:axis_data_fifo msg_fifo
set_property CONFIG.FIFO_DEPTH 16 [get_bd_cells msg_fifo]
connect_bd_intf_net [kpin solver_1 messageStream] [get_bd_intf_pins msg_fifo/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins msg_fifo/M_AXIS] [kpin message_1 messageStream]

# Memory: all kernel m_axi masters -> one AXI4 master, 64-bit data
set maxi_pins [get_bd_intf_pins -of_objects [get_bd_cells *_1] -filter {MODE == Master && VLNV =~ "*aximm*"}]
set n_mi [llength $maxi_pins]
create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect mem_sc
set_property -dict [list CONFIG.NUM_SI $n_mi CONFIG.NUM_MI 1] [get_bd_cells mem_sc]
set i 0
foreach p $maxi_pins {
    connect_bd_intf_net $p [get_bd_intf_pins mem_sc/S[format %02d $i]_AXI]
    incr i
}
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 M_AXI
set_property -dict [list CONFIG.PROTOCOL AXI4 CONFIG.DATA_WIDTH 64 CONFIG.ADDR_WIDTH 32 \
    CONFIG.FREQ_HZ $freq_hz] [get_bd_intf_ports M_AXI]
connect_bd_intf_net [get_bd_intf_pins mem_sc/M00_AXI] [get_bd_intf_ports M_AXI]

# Control: one AXI4-Lite slave -> seven kernel control ports
create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect ctrl_sc
set_property -dict [list CONFIG.NUM_SI 1 CONFIG.NUM_MI [llength $ctrl_cells]] [get_bd_cells ctrl_sc]
create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S_AXI_CTRL
set_property -dict [list CONFIG.PROTOCOL AXI4LITE CONFIG.DATA_WIDTH 32 CONFIG.ADDR_WIDTH 20 \
    CONFIG.FREQ_HZ $freq_hz] [get_bd_intf_ports S_AXI_CTRL]
connect_bd_intf_net [get_bd_intf_ports S_AXI_CTRL] [get_bd_intf_pins ctrl_sc/S00_AXI]
set i 0
foreach k $ctrl_cells {
    connect_bd_intf_net [get_bd_intf_pins ctrl_sc/M[format %02d $i]_AXI] [ctrl_pin $k]
    incr i
}

# Clocks/resets for everything
foreach c [get_bd_cells] {
    foreach clk [get_bd_pins -quiet -of_objects $c -filter {TYPE == clk}] { connect_bd_net [get_bd_ports ap_clk] $clk }
    foreach rst [get_bd_pins -quiet -of_objects $c -filter {TYPE == rst}] { connect_bd_net [get_bd_ports ap_rst_n] $rst }
}

# Interrupts: only the solver's is used (completion), the others are exposed for debug
create_bd_port -dir O -type intr solver_irq
connect_bd_net [get_bd_pins solver_1/interrupt] [get_bd_ports solver_irq]

# Address map: auto-assign, then pin the control windows (sataccel_regs) and
# give every kernel master the full 32-bit space behind M_AXI.
assign_bd_address
# Two passes so no move overlaps a window that is still auto-placed:
# park every window at a scratch offset (0x80000+), then place it.
set i 0
foreach k $ctrl_cells {
    set ctrl_seg($k) [get_bd_addr_segs -of_objects [get_bd_addr_spaces S_AXI_CTRL] -filter "NAME =~ *${k}_1*"]
    if {[llength $ctrl_seg($k)] != 1} { error "control segment for $k: '$ctrl_seg($k)'" }
    set_property range 64K $ctrl_seg($k)
    set_property offset [expr {0x80000 + $i * 0x10000}] $ctrl_seg($k)
    incr i
}
foreach k $ctrl_cells { set_property offset $KERNEL_BASE($k) $ctrl_seg($k) }
foreach sp [get_bd_addr_spaces -of_objects [get_bd_cells *_1]] {
    # (each kernel's address spaces also list its own control slave; skip it)
    foreach seg [get_bd_addr_segs -quiet -of_objects $sp -filter {NAME =~ SEG_M_AXI*}] {
        set_property offset 0x0 $seg
        set_property range 4G $seg
    }
}
foreach seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces]] {
    puts "ADDRSEG $seg offset=[get_property OFFSET $seg] range=[get_property RANGE $seg]"
}

validate_bd_design
save_bd_design
generate_target all [get_files sataccel_bd.bd]
make_wrapper -files [get_files sataccel_bd.bd] -top -import
report_ip_status
# Port list of the wrapper, for sataccel_top.v
set w [open $out_dir/sataccel_bd_wrapper.ports w]
foreach p [get_bd_ports] { puts $w "[get_property DIR $p] [get_property LEFT $p] [get_property RIGHT $p] [get_property NAME $p]" }
foreach ip [get_bd_intf_ports] {
    foreach p [get_bd_intf_ports $ip] { puts $w "intf [get_property NAME $p] [get_property CONFIG.PROTOCOL $p] [get_property CONFIG.DATA_WIDTH $p] [get_property CONFIG.ID_WIDTH $p]" }
}
close $w
