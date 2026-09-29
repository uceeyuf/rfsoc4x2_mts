// SYSREF handling and SYSREF-aligned capture trigger for the RFSoC4x2 MTS design.
//
// SYS_REF_FPGA (LVDS, AP18/AR18) is sampled by PL_CLK (FPGA_REFCLK_IN from the LMK04828)
// and re-registered in the RF fabric clock (MMCM output, phase-aligned to PL_CLK). The
// re-registered copy drives user_sysref_adc / user_sysref_dac of the RF data converter.
// A capture request (async, from the PS) starts cap_start on the next SYSREF rising edge,
// so every ADC channel opens its capture window on the same fabric cycle.
//
// Copyright (c) 2026, Yijie Yu. BSD-3-Clause.

`timescale 1ns / 1ps

module mts_sync (
    input  wire pl_clk,             // BUFG of PL_CLK
    input  wire pl_sysref_p,        // SYS_REF_FPGA, LVDS
    input  wire pl_sysref_n,
    input  wire clk_rf,             // RF fabric clock
    input  wire cap_req,            // async, from AXI GPIO
    output reg  user_sysref_adc,
    output reg  user_sysref_dac,
    output reg  cap_start,          // one clk_rf pulse on a SYSREF rising edge
    output reg  sysref_seen,        // toggles on every SYSREF rising edge (status)
    output reg  sysref_pl           // SYSREF in the PL_CLK domain (for the frequency meter)
);

wire sysref_i;
IBUFDS sysref_ibuf (.I(pl_sysref_p), .IB(pl_sysref_n), .O(sysref_i));

// fabric flop, not IOB: the I/O flop cannot take hold delay at this rate
reg sysref_pin_r;
reg sysref_rf_r, sysref_rf_d;
(* ASYNC_REG = "TRUE" *) reg [1:0] req_sync;
reg req_d, armed;

always @(posedge pl_clk) begin
    sysref_pin_r <= sysref_i;
    sysref_pl    <= sysref_pin_r;
end

always @(posedge clk_rf) begin
    sysref_rf_r     <= sysref_pl;
    sysref_rf_d     <= sysref_rf_r;
    user_sysref_adc <= sysref_pl;
    user_sysref_dac <= sysref_pl;

    req_sync <= {req_sync[0], cap_req};
    req_d    <= req_sync[1];
    if (req_sync[1] && !req_d)
        armed <= 1'b1;

    cap_start <= 1'b0;
    if (sysref_rf_r && !sysref_rf_d) begin
        sysref_seen <= ~sysref_seen;
        if (armed) begin
            cap_start <= 1'b1;
            armed <= 1'b0;
        end
    end
end

initial begin
    armed = 1'b0;
    sysref_seen = 1'b0;
    cap_start = 1'b0;
end

endmodule
