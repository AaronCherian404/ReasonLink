# SAT-Accel's clock and the timing check before the bitstream (sourced by the
# hooks that sataccel_impl.tcl writes).
#
# SAT-Accel's checkpoint carries the clock of its out-of-context synthesis
# (sataccel_hw/vivado/synth_ooc.tcl: create_clock -name clk -period 10 on
# port clk). read_checkpoint -cell applies it to the net that feeds the
# cell's clk pin, in place of the SoC clock that really drives it, so every
# path between SAT-Accel or its tile and the rest of the SoC is timed as a
# crossing between unrelated 10 ns and 12.8 ns clocks, and ESP's clock groups
# do not cover the new clock. Vivado 2023.2 can neither delete a clock nor
# skip a checkpoint's constraints (read_xdc -mode out_of_context in the OOC
# run does not keep it out either), so sataccel_fix_clock replaces it with a
# copy of the SoC clock ("completely overrides", Constraints 18-1056) and
# declares the copy asynchronous to the clocks ESP declares asynchronous to
# the SoC clock (xilinx-vcu118-xcvu9p{,-eth-constraints}.xdc).

# The SoC clock that drives SAT-Accel: ESP's tile clock (net chip_refclk)
proc sataccel_soc_clock {} {
    set c [get_clocks -quiet -of_objects [get_nets -quiet chip_refclk]]
    if {[llength $c] != 1} { error "ReasonLink: expected one clock on chip_refclk, found '$c'" }
    return [get_property NAME $c]
}

proc sataccel_cell {} {
    set c [get_cells -hier -quiet -filter {REF_NAME =~ *sataccel_top && !IS_PRIMITIVE}]
    if {[llength $c] != 1} { error "ReasonLink: expected one SAT-Accel cell, found '$c'" }
    return [get_property NAME $c]
}

proc sataccel_fix_clock {cell} {
    set soc [sataccel_soc_clock]
    set pin [get_pins $cell/clk]
    set ooc [get_clocks -quiet -of_objects $pin]
    if {$ooc eq $soc || $ooc eq "sataccel_clk"} {
        puts "ReasonLink: $cell/clk is on $ooc, nothing to fix"
        return
    }
    if {[llength $ooc] != 1} { error "ReasonLink: expected one clock at $cell/clk, found '$ooc'" }
    # define the copy where the checkpoint's clock is defined, so it replaces
    # it (-master_clock would need -add; the SoC clock's source pin carries
    # only that clock)
    set at [get_property SOURCE_PINS $ooc]
    if {$at eq ""} { set at $pin }
    create_generated_clock -name sataccel_clk \
        -source [get_pins [get_property SOURCE_PINS [get_clocks $soc]]] -divide_by 1 [get_pins $at]
    if {[get_property MASTER_CLOCK [get_clocks sataccel_clk]] ne $soc} {
        error "ReasonLink: sataccel_clk derived from [get_property MASTER_CLOCK [get_clocks sataccel_clk]], not $soc"
    }
    # the overridden clock object lingers until the next timing update
    update_timing
    set now [get_clocks -quiet -of_objects $pin]
    if {$now ne "sataccel_clk"} { error "ReasonLink: clock at $cell/clk is '$now', not sataccel_clk" }
    set esp [get_clocks -quiet -of_objects [concat [get_nets -quiet {clkm clkm1 chip_refclk}] \
                                                   [get_nets -quiet -hierarchical clk125m]]]
    set async {}
    foreach c $esp {
        if {[get_property NAME $c] ne $soc} {
            set_clock_groups -asynchronous -group [get_clocks sataccel_clk] -group $c
            lappend async [get_property NAME $c]
        }
    }
    puts "ReasonLink: replaced clock $ooc at $at by sataccel_clk (copy of $soc),\
          asynchronous to {$async}; clocks now: [get_clocks]"
}

# ESP declares its two DDR controllers' clocks (nets clkm, clkm1: separate
# board oscillators c0/c1_sys_clk_p) asynchronous, but this SoC's synthesis
# keeps no net named clkm1 ("No nets matched 'clkm1'"), so that group is
# never applied and Vivado times the DDR0 -> DDR1 reset crossings as
# synchronous. Declare it again from the controllers' UI clocks (CLKOUT0 of
# each MIG's MMCM).
proc sataccel_fix_ddr_groups {} {
    set clk {}
    foreach m {gen_mig.ddrc gen_mig.ddrc1} {
        set p [get_pins -quiet $m/no_clamshell_gen.mig_inst/inst/u_ddr4_infrastructure/gen_mmcme4.u_mmcme_adv_inst/CLKOUT0]
        lappend clk [lindex [get_clocks -quiet -of_objects $p] 0]
    }
    lassign $clk c0 c1
    if {$c0 eq "" || $c1 eq "" || $c0 eq $c1} {
        puts "ReasonLink: DDR clocks not found or not distinct ('$c0', '$c1'): no group added"
        return
    }
    set_clock_groups -asynchronous -group [get_clocks $c0] -group [get_clocks $c1]
    puts "ReasonLink: DDR controller clocks $c0 and $c1 declared asynchronous (ESP's clkm/clkm1 group)"
}

# Before write_bitstream: report timing, and stop unless setup slack is at
# least -SATACCEL_MAX_VIOLATION ns (default 0) and hold slack is met
proc sataccel_check_timing {rpt} {
    set maxvio [expr {[info exists ::env(SATACCEL_MAX_VIOLATION)] ? $::env(SATACCEL_MAX_VIOLATION) : 0.0}]
    report_timing_summary -max_paths 10 -file $rpt
    report_clock_interaction -delay_type min_max -file [file rootname $rpt]_clocks.rpt
    set wns [get_property SLACK [get_timing_paths -max_paths 1 -setup]]
    set whs [get_property SLACK [get_timing_paths -max_paths 1 -hold]]
    puts "ReasonLink: timing WNS $wns ns, WHS $whs ns ($rpt)"
    foreach clk [get_clocks] {
        set p [get_timing_paths -quiet -max_paths 1 -setup -to $clk]
        if {[llength $p] && [get_property SLACK $p] < 0} {
            puts "ReasonLink:   into [get_property NAME $clk]: [get_property SLACK $p] ns,\
                  from [get_property STARTPOINT_CLOCK $p], end [get_property ENDPOINT_PIN $p]"
        }
    }
    if {$wns < -$maxvio || $whs < 0} {
        error "ReasonLink: timing not met (WNS $wns, WHS $whs; SATACCEL_MAX_VIOLATION=$maxvio)"
    }
}
