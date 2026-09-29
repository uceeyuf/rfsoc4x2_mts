// mts_sync + cap_gate: the capture window opens on the first SYSREF edge after the
// request, lasts NBEATS beats, and is identical for two gates.
// Copyright (c) 2026, Yijie Yu. BSD-3-Clause.

`timescale 1ns / 1ps

module tb_mts_sync;

reg clk = 0;
always #1 clk = ~clk;                   // 500 MHz, PL_CLK and RF clock share the phase

reg sysref = 0;
always begin                            // 7.8125 MHz SYSREF (64 PL_CLK cycles)
    #64 sysref = 1;
    #64 sysref = 0;
end

reg cap_req = 0;
reg aresetn = 0;
wire cap_start, us_adc, us_dac, seen;

mts_sync sync (
    .pl_clk(clk), .pl_sysref_p(sysref), .pl_sysref_n(~sysref), .clk_rf(clk), .cap_req(cap_req),
    .user_sysref_adc(us_adc), .user_sysref_dac(us_dac), .cap_start(cap_start), .sysref_seen(seen)
);

reg [127:0] cnt = 0;
always @(posedge clk) cnt <= cnt + 1;

wire [127:0] d0, d1;
wire v0, v1;
cap_gate #(.NBEATS(64)) g0 (.aclk(clk), .aresetn(aresetn), .cap_start(cap_start),
    .S_AXIS_tdata(cnt), .S_AXIS_tvalid(1'b1), .S_AXIS_tready(),
    .M_AXIS_tdata(d0), .M_AXIS_tvalid(v0), .M_AXIS_tready(1'b1));
cap_gate #(.NBEATS(64)) g1 (.aclk(clk), .aresetn(aresetn), .cap_start(cap_start),
    .S_AXIS_tdata(cnt), .S_AXIS_tvalid(1'b1), .S_AXIS_tready(),
    .M_AXIS_tdata(d1), .M_AXIS_tvalid(v1), .M_AXIS_tready(1'b1));

integer n0 = 0, n1 = 0, first = -1, errors = 0;
real t_edge = 0, delta = -1;
always @(posedge sysref) t_edge = $realtime;
always @(posedge clk) begin
    if (cap_start) delta = $realtime - t_edge;
    if (v0) begin
        n0 = n0 + 1;
        if (first < 0) first = d0[31:0];
    end
    if (v1) n1 = n1 + 1;
    if (v0 != v1 || (v0 && d0 != d1)) errors = errors + 1;
end

initial begin
    #20 aresetn = 1;
    #301 cap_req = 1;                   // request at an arbitrary time
    #50 cap_req = 0;
    #800;
    $display("cap_start %0.1f ns after the preceding SYSREF rising edge", delta);
    $display("beats: gate0 %0d gate1 %0d, first beat %0d, mismatches %0d", n0, n1, first, errors);
    if (n0 == 64 && n1 == 64 && errors == 0 && delta >= 0 && delta < 10)
        $display("PASS");
    else
        $display("FAIL");
    $finish;
end

endmodule
