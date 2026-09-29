@echo off
rem xsim run of tb_mts_sync (Vivado 2020.2 on the PATH). Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
cd /d %~dp0
call xvlog ../rtl/mts_sync.v ../rtl/cap_gate.v tb_mts_sync.v %XILINX_VIVADO%/data/verilog/src/glbl.v || exit /b 1
call xelab -L unisims_ver tb_mts_sync glbl -R
