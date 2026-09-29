# Program the RFSoC4x2 over JTAG and start the application on A53 #0.
#   xsct run_jtag.tcl
# Copyright (c) 2026, Yijie Yu. BSD-3-Clause.

set here [file normalize [file dirname [info script]]]
set hw   $here/vitis_ws/mts_pf/export/mts_pf/hw
set xsa  [lindex [glob $hw/*.xsa] 0]
set bit  [lindex [glob $hw/*.bit] 0]
set elf  $here/vitis_ws/mts_jtag/Debug/mts_jtag.elf

connect
targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 3000
targets -set -nocase -filter {name =~ "PSU*"}
fpga -file $bit
targets -set -nocase -filter {name =~ "APU*"}
loadhw -hw $xsa -mem-ranges [list {0x80000000 0xbfffffff} {0x400000000 0x5ffffffff} {0x1000000000 0x7fffffffff}] -regs
configparams force-mem-access 1
source $hw/psu_init.tcl
psu_init
after 1000
psu_ps_pl_isolation_removal
after 1000
psu_ps_pl_reset_config
catch {psu_protection}
targets -set -nocase -filter {name =~ "*A53*#0"}
rst -processor
dow $elf
configparams force-mem-access 0
con
puts "running $elf"
