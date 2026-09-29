# Read the four 64 k sample ADC captures and the DAC waveform over JTAG while the
# application runs (after a 'c' on the UART).
#   xsct dump_captures.tcl [output directory, default ../out]
# Copyright (c) 2026, Yijie Yu. BSD-3-Clause.

set here [file normalize [file dirname [info script]]]
set out  [expr {[llength $argv] > 0 ? [file normalize [lindex $argv 0]] : "$here/../out"}]
file mkdir $out

connect
targets -set -nocase -filter {name =~ "*A53*#0"}
set names {adc_d adc_c adc_b adc_a}
for {set i 0} {$i < 4} {incr i} {
    set addr [expr {0xA0200000 + $i * 0x80000}]
    mrd -force -size w -bin -file $out/[lindex $names $i].bin $addr 32768
}
mrd -force -size w -bin -file $out/dac.bin 0xA0100000 32768
puts "captures written to $out (int16, 65536 samples each, 4.0 GSPS)"
