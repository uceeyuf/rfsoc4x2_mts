// Capture window for one ADC stream in the RF fabric clock domain.
// Passes NBEATS beats of the RF data converter stream after cap_start and drops the rest,
// so all channels hand the same sample window to their (asynchronous) clock converters.
//
// Copyright (c) 2026, Yijie Yu. BSD-3-Clause.

`timescale 1ns / 1ps

module cap_gate #(
    parameter DWIDTH = 128,
    parameter NBEATS = 8192             // 8192 x 8 samples = 65536 samples
) (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS:M_AXIS, ASSOCIATED_RESET aresetn" *)
    input  wire              aclk,
    input  wire              aresetn,
    input  wire              cap_start,

    input  wire [DWIDTH-1:0] S_AXIS_tdata,
    input  wire              S_AXIS_tvalid,
    output wire              S_AXIS_tready,

    output reg  [DWIDTH-1:0] M_AXIS_tdata,
    output reg               M_AXIS_tvalid,
    input  wire              M_AXIS_tready
);

reg [$clog2(NBEATS+1)-1:0] left;

assign S_AXIS_tready = 1'b1;           // the converter output cannot be stalled

always @(posedge aclk) begin
    M_AXIS_tdata  <= S_AXIS_tdata;
    M_AXIS_tvalid <= S_AXIS_tvalid && (left != 0);
    if (!aresetn)
        left <= 0;
    else if (cap_start)
        left <= NBEATS;
    else if (S_AXIS_tvalid && left != 0)
        left <= left - 1'b1;
end

endmodule
