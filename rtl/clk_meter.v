// Free-running PL_CLK cycle counter and SYSREF edge counter, Gray coded so the PS can
// sample them asynchronously through an AXI GPIO and compute both frequencies.
// Runs on PL_CLK directly (before the MMCM), so it works even when the MMCM cannot lock.
//
// Copyright (c) 2026, Yijie Yu. BSD-3-Clause.

`timescale 1ns / 1ps

module clk_meter (
    input  wire        pl_clk,
    input  wire        sysref_pl,
    output reg  [31:0] clk_count_gray,
    output reg  [31:0] sysref_count_gray
);

reg [31:0] clk_count = 0, sysref_count = 0;
reg sysref_d = 0;

always @(posedge pl_clk) begin
    clk_count <= clk_count + 1'b1;
    sysref_d  <= sysref_pl;
    if (sysref_pl && !sysref_d)
        sysref_count <= sysref_count + 1'b1;
    clk_count_gray    <= clk_count ^ (clk_count >> 1);
    sysref_count_gray <= sysref_count ^ (sysref_count >> 1);
end

endmodule
