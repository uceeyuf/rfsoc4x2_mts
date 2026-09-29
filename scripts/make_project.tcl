# RFSoC4x2 multi-tile synchronization over JTAG, Vivado 2020.2 (no PYNQ)
#
#   vivado -mode batch -source scripts/make_project.tcl                 (project + block design)
#   vivado -mode batch -source scripts/make_project.tcl -tclargs build  (+ bitstream + .xsa)
#
# 4.0 GSPS. DAC tiles 228 (DAC_B) and 230 (DAC_A) play one waveform from URAM; ADC tiles
# 224 (ADC_D, ADC_C) and 226 (ADC_B, ADC_A) are captured into URAM, 64 k samples each,
# on the same SYSREF-aligned fabric cycle. Tile 2 PLLs run from the 500 MHz LMX2594
# reference and distribute the sample clock to tiles 1 and 0. PL_CLK / PL_SYSREF come
# from the LMK04828 (500 MHz). Clock chips are programmed by the bare-metal app (PS SPI0).
#
# Copyright (c) 2026, Yijie Yu. BSD-3-Clause.

set build [expr {[llength $argv] > 0 && [lindex $argv 0] eq "build"}]

set here     [file normalize [file dirname [info script]]/..]
set proj_dir $here/build
set bd_name  mts

create_project mts_jtag $proj_dir -part xczu48dr-ffvg1517-2-e -force
set_property board_part realdigital.org:rfsoc4x2:part0:1.0 [current_project]
set_property target_language Verilog [current_project]

add_files -norecurse [list \
    $here/third_party/rfsoc_mts/DACRAMstreamer.v \
    $here/third_party/rfsoc_mts/ADCRAMcapture.v \
    $here/rtl/mts_sync.v \
    $here/rtl/cap_gate.v \
    $here/rtl/clk_meter.v]
add_files -fileset constrs_1 -norecurse $here/constraints/mts.xdc
update_compile_order -fileset sources_1

create_bd_design $bd_name

# ---------------------------------------------------------------- PS
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e zynq_ultra_ps_e_0]
apply_bd_automation -rule xilinx.com:bd_rule:zynq_ultra_ps_e -config {apply_board_preset "1"} $ps
set_property -dict [list \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
    CONFIG.PSU__USE__M_AXI_GP1 {0} \
    CONFIG.PSU__USE__M_AXI_GP2 {0} \
    CONFIG.PSU__FPGA_PL0_ENABLE {1} \
    CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ {100} \
    CONFIG.PSU__DDRC__ROW_ADDR_COUNT {16} \
] $ps
set clk_ps [get_bd_pins zynq_ultra_ps_e_0/pl_clk0]

# ---------------------------------------------------------------- RF data converter
set rfdc [create_bd_cell -type ip -vlnv xilinx.com:ip:usp_rf_data_converter usp_rf_data_converter_0]
set cfg [list CONFIG.Axiclk_Freq {100.0}]
# all four tiles on: the analog SYSREF is chained through every tile (DAC side, then
# ADC tile 3 -> 2 -> 1 -> 0), a disabled tile breaks ADC MTS
foreach t {0 1 2 3} {
    if {$t == 2} {set pll true; set ref 500.000} else {set pll false; set ref 4000.000}
    foreach type {ADC DAC} {
        lappend cfg CONFIG.${type}${t}_Enable {1} \
                    CONFIG.${type}${t}_Multi_Tile_Sync {true} \
                    CONFIG.${type}${t}_Sampling_Rate {4.0} \
                    CONFIG.${type}${t}_Fabric_Freq {500.000} \
                    CONFIG.${type}${t}_Outclk_Freq {250.000} \
                    CONFIG.${type}${t}_PLL_Enable $pll \
                    CONFIG.${type}${t}_Refclk_Freq $ref
    }
    lappend cfg CONFIG.ADC${t}_Clock_Source {2} CONFIG.DAC${t}_Clock_Source {6}
}
lappend cfg CONFIG.ADC2_Clock_Dist {2} CONFIG.DAC2_Clock_Dist {2}
# ADC: real, x1, 8 samples / cycle; slices 0 and 2 of tiles 0 and 2, slice 0 of tiles 1 and 3
foreach s {00 02 10 20 22 30} {
    lappend cfg CONFIG.ADC_Slice${s}_Enable {true} CONFIG.ADC_Data_Width${s} {8} \
                CONFIG.ADC_Decimation_Mode${s} {1} CONFIG.ADC_Mixer_Type${s} {1} \
                CONFIG.ADC_Coarse_Mixer_Freq${s} {3}
}
# DAC: real, x1, 8 samples / cycle; channel 0 of tiles 0 .. 3
foreach s {00 10 20 30} {
    lappend cfg CONFIG.DAC_Slice${s}_Enable {true} CONFIG.DAC_Data_Width${s} {8} \
                CONFIG.DAC_Interpolation_Mode${s} {1} CONFIG.DAC_Mixer_Type${s} {1} \
                CONFIG.DAC_Coarse_Mixer_Freq${s} {3}
}
set_property -dict $cfg $rfdc

foreach p {adc2_clk dac2_clk sysref_in vin0_01 vin0_23 vin2_01 vin2_23 vout00 vout20} {
    make_bd_intf_pins_external [get_bd_intf_pins usp_rf_data_converter_0/$p]
}

# ---------------------------------------------------------------- PL clocks
# FPGA_REFCLK_IN (AN11/AP11) and SYS_REF_FPGA (AP18/AR18) are LVDS pairs (RFSoC4x2 manual)
set plclk_port [create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 PL_CLK]
set_property CONFIG.FREQ_HZ 500000000 $plclk_port
create_bd_port -dir I PL_SYSREF_P
create_bd_port -dir I PL_SYSREF_N

set ibuf [create_bd_cell -type ip -vlnv xilinx.com:ip:util_ds_buf ibufds_pl_clk]
set_property CONFIG.C_BUF_TYPE {IBUFDS} $ibuf
connect_bd_intf_net [get_bd_intf_ports PL_CLK] [get_bd_intf_pins ibufds_pl_clk/CLK_IN_D]
set bufg [create_bd_cell -type ip -vlnv xilinx.com:ip:util_ds_buf bufg_pl_clk]
set_property CONFIG.C_BUF_TYPE {BUFG} $bufg
connect_bd_net [get_bd_pins ibufds_pl_clk/IBUF_OUT] [get_bd_pins bufg_pl_clk/BUFG_I]
set pl_clk [get_bd_pins bufg_pl_clk/BUFG_O]

set mmcm [create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz mmcm_rf]
set_property -dict [list \
    CONFIG.PRIM_SOURCE {Global_buffer} \
    CONFIG.PRIM_IN_FREQ {500.000} \
    CONFIG.PRIMITIVE {MMCM} \
    CONFIG.USE_PHASE_ALIGNMENT {true} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {500.000} \
    CONFIG.CLKOUT2_USED {true} \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {250.000} \
    CONFIG.NUM_OUT_CLKS {2} \
    CONFIG.USE_RESET {true} \
    CONFIG.RESET_TYPE {ACTIVE_HIGH} \
    CONFIG.USE_LOCKED {true} \
] $mmcm
connect_bd_net $pl_clk [get_bd_pins mmcm_rf/clk_in1]
set clk_rf  [get_bd_pins mmcm_rf/clk_out1]
set clk_cap [get_bd_pins mmcm_rf/clk_out2]

# control / status GPIO: ch1 out [0] DAC play, [1] capture request, [2] capture arm,
# [3] MMCM reset; ch2 in [0] MMCM locked, [1] SYSREF toggle
set gpio [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio gpio_ctrl]
set_property -dict [list CONFIG.C_ALL_OUTPUTS {1} CONFIG.C_GPIO_WIDTH {4} \
    CONFIG.C_IS_DUAL {1} CONFIG.C_ALL_INPUTS_2 {1} CONFIG.C_GPIO2_WIDTH {2}] $gpio
foreach {name i} {play 0 cap_req 1 cap_arm 2 mmcm_rst 3} {
    set s [create_bd_cell -type ip -vlnv xilinx.com:ip:xlslice gpio_$name]
    set_property -dict [list CONFIG.DIN_WIDTH {4} CONFIG.DIN_FROM $i CONFIG.DIN_TO $i] $s
    connect_bd_net [get_bd_pins gpio_ctrl/gpio_io_o] [get_bd_pins gpio_$name/Din]
}
connect_bd_net [get_bd_pins gpio_mmcm_rst/Dout] [get_bd_pins mmcm_rf/reset]

# resets
foreach {name clk} [list rst_ps $clk_ps rst_rf $clk_rf rst_cap $clk_cap] {
    create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset $name
    connect_bd_net $clk [get_bd_pins $name/slowest_sync_clk]
    connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_resetn0] [get_bd_pins $name/ext_reset_in]
}
connect_bd_net [get_bd_pins mmcm_rf/locked] [get_bd_pins rst_rf/dcm_locked] [get_bd_pins rst_cap/dcm_locked]
set rstn_ps  [get_bd_pins rst_ps/peripheral_aresetn]
set rstn_rf  [get_bd_pins rst_rf/peripheral_aresetn]
set rstn_cap [get_bd_pins rst_cap/peripheral_aresetn]

# SYSREF + capture trigger
create_bd_cell -type module -reference mts_sync mts_sync_0
connect_bd_net $pl_clk [get_bd_pins mts_sync_0/pl_clk]
connect_bd_net [get_bd_ports PL_SYSREF_P] [get_bd_pins mts_sync_0/pl_sysref_p]
connect_bd_net [get_bd_ports PL_SYSREF_N] [get_bd_pins mts_sync_0/pl_sysref_n]

# PL_CLK / SYSREF frequency meter (runs before the MMCM), read through gpio_meter
create_bd_cell -type module -reference clk_meter clk_meter_0
connect_bd_net $pl_clk [get_bd_pins clk_meter_0/pl_clk]
connect_bd_net [get_bd_pins mts_sync_0/sysref_pl] [get_bd_pins clk_meter_0/sysref_pl]
set meter [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio gpio_meter]
set_property -dict [list CONFIG.C_ALL_INPUTS {1} CONFIG.C_GPIO_WIDTH {32} \
    CONFIG.C_IS_DUAL {1} CONFIG.C_ALL_INPUTS_2 {1} CONFIG.C_GPIO2_WIDTH {32}] $meter
connect_bd_net [get_bd_pins clk_meter_0/clk_count_gray] [get_bd_pins gpio_meter/gpio_io_i]
connect_bd_net [get_bd_pins clk_meter_0/sysref_count_gray] [get_bd_pins gpio_meter/gpio2_io_i]
connect_bd_net $clk_rf [get_bd_pins mts_sync_0/clk_rf]
connect_bd_net [get_bd_pins gpio_cap_req/Dout] [get_bd_pins mts_sync_0/cap_req]
connect_bd_net [get_bd_pins mts_sync_0/user_sysref_adc] [get_bd_pins usp_rf_data_converter_0/user_sysref_adc]
connect_bd_net [get_bd_pins mts_sync_0/user_sysref_dac] [get_bd_pins usp_rf_data_converter_0/user_sysref_dac]

set stat [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat gpio_status]
set_property CONFIG.NUM_PORTS {2} $stat
connect_bd_net [get_bd_pins mmcm_rf/locked] [get_bd_pins gpio_status/In0]
connect_bd_net [get_bd_pins mts_sync_0/sysref_seen] [get_bd_pins gpio_status/In1]
connect_bd_net [get_bd_pins gpio_status/dout] [get_bd_pins gpio_ctrl/gpio2_io_i]

foreach t {0 1 2 3} {
    connect_bd_net $clk_rf  [get_bd_pins usp_rf_data_converter_0/s${t}_axis_aclk] [get_bd_pins usp_rf_data_converter_0/m${t}_axis_aclk]
    connect_bd_net $rstn_rf [get_bd_pins usp_rf_data_converter_0/s${t}_axis_aresetn] [get_bd_pins usp_rf_data_converter_0/m${t}_axis_aresetn]
}

# ---------------------------------------------------------------- AXI-Lite / memory-mapped control
set ic [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect ctrl_interconnect]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {8}] $ic
connect_bd_intf_net [get_bd_intf_pins zynq_ultra_ps_e_0/M_AXI_HPM0_FPD] [get_bd_intf_pins ctrl_interconnect/S00_AXI]
connect_bd_net $clk_ps [get_bd_pins zynq_ultra_ps_e_0/maxihpm0_fpd_aclk] [get_bd_pins ctrl_interconnect/ACLK] \
    [get_bd_pins ctrl_interconnect/S00_ACLK]
connect_bd_net [get_bd_pins rst_ps/interconnect_aresetn] [get_bd_pins ctrl_interconnect/ARESETN]
connect_bd_net $rstn_ps [get_bd_pins ctrl_interconnect/S00_ARESETN]

# URAM-backed memory (AXI BRAM controller on port A, streamer / capture on port B)
proc make_uram {name} {
    set ctrl [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_bram_ctrl ${name}_ctrl]
    set_property -dict [list CONFIG.DATA_WIDTH {256} CONFIG.SINGLE_PORT_BRAM {1} \
        CONFIG.ECC_TYPE {0} CONFIG.READ_LATENCY {3}] $ctrl
    set mem [create_bd_cell -type ip -vlnv xilinx.com:ip:blk_mem_gen ${name}_mem]
    set_property -dict [list CONFIG.Memory_Type {True_Dual_Port_RAM} \
        CONFIG.PRIM_type_to_Implement {URAM} CONFIG.Assume_Synchronous_Clk {true} \
        CONFIG.Enable_B {Use_ENB_Pin} CONFIG.Use_RSTB_Pin {true} \
        CONFIG.Operating_Mode_A {NO_CHANGE} CONFIG.Operating_Mode_B {NO_CHANGE} \
        CONFIG.READ_LATENCY_A {3} CONFIG.READ_LATENCY_B {3} CONFIG.EN_SAFETY_CKT {false}] $mem
    connect_bd_intf_net [get_bd_intf_pins ${name}_ctrl/BRAM_PORTA] [get_bd_intf_pins ${name}_mem/BRAM_PORTA]
}

# DAC player: URAM (250 MHz, 256 bit) -> 500 MHz, 128 bit -> tiles 0, 1, 2
make_uram dac_play
create_bd_cell -type module -reference DACRAMstreamer dac_streamer
set_property CONFIG.MEM_SIZE_BYTES {131072} [get_bd_cells dac_streamer]
connect_bd_intf_net [get_bd_intf_pins dac_streamer/BRAM_A] [get_bd_intf_pins dac_play_mem/BRAM_PORTB]
set nvec [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant dac_nvec]
set_property -dict [list CONFIG.CONST_WIDTH {12} CONFIG.CONST_VAL {0xFFF}] $nvec
connect_bd_net [get_bd_pins dac_nvec/dout] [get_bd_pins dac_streamer/numSampleVectors]
connect_bd_net [get_bd_pins gpio_play/Dout] [get_bd_pins dac_streamer/enable]

set dcc [create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter dac_cc]
set_property CONFIG.IS_ACLK_ASYNC {1} $dcc
set ddw [create_bd_cell -type ip -vlnv xilinx.com:ip:axis_dwidth_converter dac_dw]
set_property CONFIG.M_TDATA_NUM_BYTES {16} $ddw
set bc [create_bd_cell -type ip -vlnv xilinx.com:ip:axis_broadcaster dac_bcast]
set_property -dict [list CONFIG.NUM_MI {4} CONFIG.S_TDATA_NUM_BYTES {16} CONFIG.M_TDATA_NUM_BYTES {16}] $bc
connect_bd_intf_net [get_bd_intf_pins dac_streamer/axis] [get_bd_intf_pins dac_cc/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins dac_cc/M_AXIS] [get_bd_intf_pins dac_dw/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins dac_dw/M_AXIS] [get_bd_intf_pins dac_bcast/S_AXIS]
foreach {m s} {M00 s00 M01 s10 M02 s20 M03 s30} {
    connect_bd_intf_net [get_bd_intf_pins dac_bcast/${m}_AXIS] [get_bd_intf_pins usp_rf_data_converter_0/${s}_axis]
}
connect_bd_net $clk_cap [get_bd_pins dac_streamer/axis_clk] [get_bd_pins dac_play_ctrl/s_axi_aclk] [get_bd_pins dac_cc/s_axis_aclk]
connect_bd_net $clk_rf [get_bd_pins dac_cc/m_axis_aclk] [get_bd_pins dac_dw/aclk] [get_bd_pins dac_bcast/aclk]
connect_bd_net $rstn_cap [get_bd_pins dac_streamer/axis_aresetn] [get_bd_pins dac_play_ctrl/s_axi_aresetn] [get_bd_pins dac_cc/s_axis_aresetn]
connect_bd_net $rstn_rf [get_bd_pins dac_cc/m_axis_aresetn] [get_bd_pins dac_dw/aresetn] [get_bd_pins dac_bcast/aresetn]

# ADC captures: RF stream -> SYSREF-aligned window (500 MHz) -> 256 bit -> 250 MHz -> URAM
set captures {cap0 m00 cap1 m02 cap2 m20 cap3 m22}
foreach {c m} $captures {
    create_bd_cell -type module -reference cap_gate ${c}_gate
    set dw [create_bd_cell -type ip -vlnv xilinx.com:ip:axis_dwidth_converter ${c}_dw]
    set_property CONFIG.M_TDATA_NUM_BYTES {32} $dw
    set cc [create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter ${c}_cc]
    set_property CONFIG.IS_ACLK_ASYNC {1} $cc
    create_bd_cell -type module -reference ADCRAMcapture ${c}_writer
    set_property CONFIG.MEM_SIZE_BYTES {131072} [get_bd_cells ${c}_writer]
    make_uram $c

    connect_bd_intf_net [get_bd_intf_pins usp_rf_data_converter_0/${m}_axis] [get_bd_intf_pins ${c}_gate/S_AXIS]
    connect_bd_intf_net [get_bd_intf_pins ${c}_gate/M_AXIS] [get_bd_intf_pins ${c}_dw/S_AXIS]
    connect_bd_intf_net [get_bd_intf_pins ${c}_dw/M_AXIS] [get_bd_intf_pins ${c}_cc/S_AXIS]
    connect_bd_intf_net [get_bd_intf_pins ${c}_cc/M_AXIS] [get_bd_intf_pins ${c}_writer/CAP_AXIS]
    connect_bd_intf_net [get_bd_intf_pins ${c}_writer/BRAM_A] [get_bd_intf_pins ${c}_mem/BRAM_PORTB]
    connect_bd_net [get_bd_pins mts_sync_0/cap_start] [get_bd_pins ${c}_gate/cap_start]
    connect_bd_net [get_bd_pins gpio_cap_arm/Dout] [get_bd_pins ${c}_writer/trig_cap]
    connect_bd_net $clk_rf [get_bd_pins ${c}_gate/aclk] [get_bd_pins ${c}_dw/aclk] [get_bd_pins ${c}_cc/s_axis_aclk]
    connect_bd_net $rstn_rf [get_bd_pins ${c}_gate/aresetn] [get_bd_pins ${c}_dw/aresetn] [get_bd_pins ${c}_cc/s_axis_aresetn]
    connect_bd_net $clk_cap [get_bd_pins ${c}_cc/m_axis_aclk] [get_bd_pins ${c}_writer/axis_clk] [get_bd_pins ${c}_ctrl/s_axi_aclk]
    connect_bd_net $rstn_cap [get_bd_pins ${c}_cc/m_axis_aresetn] [get_bd_pins ${c}_writer/axis_aresetn] [get_bd_pins ${c}_ctrl/s_axi_aresetn]
}

# interconnect masters: M00 rfdc, M01 gpio (100 MHz); M02 DAC URAM, M03..M06 captures (250 MHz)
set slaves [list \
    M00 usp_rf_data_converter_0/s_axi $clk_ps  $rstn_ps \
    M01 gpio_ctrl/S_AXI               $clk_ps  $rstn_ps \
    M02 dac_play_ctrl/S_AXI           $clk_cap $rstn_cap \
    M03 cap0_ctrl/S_AXI               $clk_cap $rstn_cap \
    M04 cap1_ctrl/S_AXI               $clk_cap $rstn_cap \
    M05 cap2_ctrl/S_AXI               $clk_cap $rstn_cap \
    M06 cap3_ctrl/S_AXI               $clk_cap $rstn_cap \
    M07 gpio_meter/S_AXI              $clk_ps  $rstn_ps \
]
foreach {m slave clk rstn} $slaves {
    connect_bd_intf_net [get_bd_intf_pins ctrl_interconnect/${m}_AXI] [get_bd_intf_pins $slave]
    connect_bd_net $clk  [get_bd_pins ctrl_interconnect/${m}_ACLK]
    connect_bd_net $rstn [get_bd_pins ctrl_interconnect/${m}_ARESETN]
}
connect_bd_net $clk_ps [get_bd_pins usp_rf_data_converter_0/s_axi_aclk] [get_bd_pins gpio_ctrl/s_axi_aclk] \
    [get_bd_pins gpio_meter/s_axi_aclk]
connect_bd_net $rstn_ps [get_bd_pins usp_rf_data_converter_0/s_axi_aresetn] [get_bd_pins gpio_ctrl/s_axi_aresetn] \
    [get_bd_pins gpio_meter/s_axi_aresetn]

# ---------------------------------------------------------------- address map
set addr_map {
    usp_rf_data_converter_0/s_axi   0xA0000000 256K
    gpio_ctrl/S_AXI                 0xA0040000 4K
    gpio_meter/S_AXI                0xA0041000 4K
    dac_play_ctrl/S_AXI             0xA0100000 128K
    cap0_ctrl/S_AXI                 0xA0200000 128K
    cap1_ctrl/S_AXI                 0xA0280000 128K
    cap2_ctrl/S_AXI                 0xA0300000 128K
    cap3_ctrl/S_AXI                 0xA0380000 128K
}
foreach {pin base range} $addr_map {
    assign_bd_address -offset $base -range $range \
        -target_address_space [get_bd_addr_spaces zynq_ultra_ps_e_0/Data] \
        [get_bd_addr_segs -of_objects [get_bd_intf_pins $pin]]
}

validate_bd_design
save_bd_design
make_wrapper -files [get_files $bd_name.bd] -top -import
set_property top ${bd_name}_wrapper [current_fileset]
update_compile_order -fileset sources_1

if {$build} {
    launch_runs impl_1 -to_step write_bitstream -jobs 8
    wait_on_run impl_1
    if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
        error "implementation failed"
    }
    open_run impl_1
    report_timing_summary -file $here/build/timing_summary.rpt
    report_utilization -file $here/build/utilization.rpt
    write_hw_platform -fixed -include_bit -force -file $here/build/mts_wrapper.xsa
}
