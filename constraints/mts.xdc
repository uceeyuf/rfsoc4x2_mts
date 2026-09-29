## RFSoC4x2 MTS over JTAG
## FPGA_REFCLK_IN (AN11/AP11) and SYS_REF_FPGA (AP18/AR18) from the LMK04828 are LVDS
## pairs in HP banks 65/66 (RFSoC 4x2 reference manual), terminated on die.
set_property PACKAGE_PIN AN11 [get_ports PL_CLK_clk_p]
set_property PACKAGE_PIN AP11 [get_ports PL_CLK_clk_n]
set_property PACKAGE_PIN AP18 [get_ports PL_SYSREF_P]
set_property PACKAGE_PIN AR18 [get_ports PL_SYSREF_N]
set_property IOSTANDARD LVDS [get_ports {PL_CLK_clk_p PL_CLK_clk_n PL_SYSREF_P PL_SYSREF_N}]
set_property DIFF_TERM_ADV TERM_100 [get_ports {PL_CLK_clk_p PL_CLK_clk_n PL_SYSREF_P PL_SYSREF_N}]

create_clock -period 2.000 -name PL_CLK [get_ports PL_CLK_clk_p]

## SYSREF is captured by PL_CLK (PG269 MTS requirements)
set_input_delay -clock [get_clocks PL_CLK] -min -add_delay 2.000 [get_ports PL_SYSREF_P]
set_input_delay -clock [get_clocks PL_CLK] -max -add_delay 2.031 [get_ports PL_SYSREF_P]

## PS-driven control bits are quasi static; the meter counters are Gray coded
set_false_path -from [get_cells -hier -filter {NAME =~ *gpio_ctrl*/gpio_core_1/*gpio_Data_Out_reg*}]
set_false_path -from [get_cells -hier -filter {NAME =~ *clk_meter_0*count_gray_reg*}]

set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
