![语言](https://img.shields.io/badge/语言-Verilog_+_C-9A90FD.svg) ![仿真](https://img.shields.io/badge/仿真-xsim-green.svg) ![部署](https://img.shields.io/badge/部署-vivado_2020.2-FF1010.svg) ![板卡](https://img.shields.io/badge/板卡-RFSoC_4x2-blue.svg)

[English](#en) | [中文](#cn)

　

<span id="en">RFSoC 4x2 Multi-Tile Synchronization</span>
===========================

Multi-tile synchronization (MTS) of the RF data converter on the RFSoC 4x2, bare metal over JTAG (no PYNQ), checked with a two-channel loopback at **4.0 GSPS**: **DAC_A (tile 230) → ADC_B (tile 226)** and **DAC_B (tile 228) → ADC_D (tile 224)**. Both DACs play the same waveform and both ADCs capture on the same fabric cycle, so the delay between the two received channels contains the DAC and ADC tile skew.

On top of MTS, a wideband chirp training sequence and a sub-sample cross-correlation estimator measure the residual skew between the channels, and a closed-loop correction with the RFDC coarse delay removes it to the nearest sample.

Measured on the board: without MTS the delay changes by up to about 100 samples every time the tiles restart; with MTS it is a constant **−1.00 sample (−251 ps)**, and a short training after MTS removes that too: **−0.002 samples (−0.5 ps)** in every run.

　

| ![arch](./docs/img/arch.svg) |
| :--------------------------: |
| **Figure1** : design         |

　

## Technical Features

* **Clocks without PYNQ**: LMK04828 and two LMX2594 are programmed over PS SPI0 with the PYNQ `LMK04828_500.0` / `LMX2594_500.0` register sets: 500 MHz reference to the tile 2 PLLs, **PL_CLK 500 MHz and SYSREF 5 MHz as LVDS** into the PL (AN11/AP11, AP18/AR18). The tile 2 PLLs make 4 GHz and distribute it to the other tiles.
* **All four RF tiles enabled**: the analog SYSREF is chained through every ADC tile, so tiles 225 / 227 are enabled although they are not bonded out on the 4x2. DAC tiles 0 / 2 and ADC tiles 0 / 2 are synchronized, reference tile 2.
* **SYSREF in the fabric** (`rtl/mts_sync.v`): PL_SYSREF sampled by PL_CLK and re-registered in the 500 MHz RF fabric clock, driving `user_sysref_adc` / `user_sysref_dac`.
* **SYSREF-aligned capture** (`rtl/cap_gate.v`): a capture request from the PS opens the window of all four ADC streams on the next SYSREF rising edge, before the clock-crossing FIFOs, so the captures start on the same sample.
* **Measurement on the board**: a 4096-sample chirp (100 MHz → 1.5 GHz) from a URAM player, 64 k sample URAM captures, cross-correlation over ±512 samples with parabolic interpolation (ADC_D is inverted on the 4x2, the sign of the peak is taken into account).
* **Alignment by training** (`sw/src/align.c`): MTS lines up the tiles but not the analog paths. After MTS the chirp delay is measured, the earlier ADC is delayed with the RFDC coarse delay (one step = one sample at 4 GSPS, verified by a sweep) and the delay is measured again until the two channels are within half a sample.
* **Status**: PL_CLK / SYSREF frequency meter, MMCM lock, RF tile states and MTS latencies on the UART; the application refuses PL accesses while the fabric clock is missing.

　

## Board Results

UART key `x`: 5 × (restart tiles, measure, run MTS, measure, align). Delay of ADC_D vs ADC_B, 1 sample = 250 ps:

| Run | Without MTS | With MTS | MTS + alignment |
| :-: | ----------: | -------: | --------------: |
| 1   | +25.530     | −1.002   | **−0.002**      |
| 2   | −12.515     | −1.000   | **−0.000**      |
| 3   | +11.967     | −1.002   | **−0.002**      |
| 4   | +27.918     | −1.002   | **−0.002**      |
| 5   | +5.410      | −1.002   | **−0.002**      |

MTS latencies: DAC 120 / 120 (offsets 0 / 8), ADC 88 / 88 (offsets 0 / 0). The −1 sample left by MTS is the same in every run; training puts 1 step of coarse delay on ADC_D. Coarse delay sweep (key `d`): ADC_D 0 / 1 / 2 / 3 steps → −1.001 / −0.001 / +0.999 / +1.999 samples; the DAC coarse delay does not change the loopback delay in this configuration, so the correction is on the ADC side. Full log: [docs/results/board_log.txt](./docs/results/board_log.txt).

| ![captures](./docs/img/mts_captures.png)                                         |
| :------------------------------------------------------------------------------: |
| **Figure2** : the two received chirps without MTS, with MTS and after alignment |

Simulation (`sim/run_sim.bat`): the capture window opens 9 ns after the SYSREF edge, both gates deliver identical data. Implementation (Vivado 2020.2): 32.4 k LUT (7.6 %), 36.0 k FF, 20 URAM, WNS +0.023 ns ([timing](./docs/results/timing_summary.rpt), [utilization](./docs/results/utilization.rpt)).

　

## Build and Run

Vivado / Vitis 2020.2 with the RFSoC 4x2 board files; JTAG and UART1 (115200) on the micro-USB port; two SMA cables DAC_A → ADC_B and DAC_B → ADC_D.

```
vivado -mode batch -source scripts/make_project.tcl -tclargs build   # build/mts_wrapper.xsa
xsct sw/create_vitis.tcl                                             # standalone, mts_jtag.elf
xsct sw/run_jtag.tcl                                                 # bitstream + ELF over JTAG
```

UART keys: `x` experiment, `c` capture + measure, `m` MTS, `a` align by training, `d` coarse delay sweep, `r` restart tiles (clears MTS), `1` / `2` sine / chirp, `p` DAC on/off, `s` status, `k` reprogram the clocks (if LMK PLL1 was still settling after power-on).

Plot captures: after a `c`, `xsct sw/dump_captures.tcl out`, then `python host/plot_captures.py out` (or several directories: without MTS / with MTS / aligned).

```
rtl/            mts_sync.v, cap_gate.v, clk_meter.v
third_party/    DACRAMstreamer.v, ADCRAMcapture.v (AMD RFSoC-MTS)
constraints/    mts.xdc
scripts/        make_project.tcl (block design, bitstream, .xsa)
sw/src/         main.c, rf_mts.c (RFDC + MTS), align.c (training), capture.c (waveform, delay), LMK_LMX.c (SPI clocks)
sim/            tb_mts_sync.v, run_sim.bat
host/           plot_captures.py
```

　

## Citation

If this work helps your research, please cite it:

```bibtex
@misc{yu2026rfsoc4x2_mts,
    author = {Yijie Yu},
    title = {{RFSoC 4x2 Multi-Tile Synchronization}},
    year = {2026},
    howpublished = {\url{https://github.com/uceeyuf/rfsoc4x2_mts}},
    note = {GitHub repository},
}
```

GitHub also offers the citation under **Cite this repository** (from [CITATION.cff](CITATION.cff)).

　

## Credits

URAM player / capture blocks and the design approach: [Xilinx/RFSoC-MTS](https://github.com/Xilinx/RFSoC-MTS) (MIT, `third_party/rfsoc_mts/LICENSE`); clock register values: PYNQ RFSoC4x2 `LMK04828_500.0` / `LMX2594_500.0`; SPI driver from [RFSoC4x2_clock_LMK_LMX](https://github.com/uceeyuf/RFSoC4x2_clock_LMK_LMX); RF data converter driver: Xilinx `rfdc`. Everything else: BSD 3-Clause, Copyright (c) 2026, Yijie Yu.

　

　

<span id="cn">RFSoC 4x2 多 Tile 同步</span>
===========================

在 RFSoC 4x2 上实现 RF 数据转换器的多 tile 同步（MTS），裸机运行、JTAG 加载，不依赖 PYNQ，用 **4.0 GSPS** 双通道环回验证：**DAC_A（tile 230）→ ADC_B（tile 226）**，**DAC_B（tile 228）→ ADC_D（tile 224）**。两个 DAC 播放同一波形，两个 ADC 在同一 fabric 周期开始采集，所以两路接收信号之间的延迟包含了 DAC 和 ADC 的 tile 间偏差。

在 MTS 之上，用宽带 chirp 训练序列和亚采样点精度的互相关估计器测量两路之间剩余的偏差，再用 RFDC 粗延迟做闭环校正，精确到一个采样点。

上板实测：不开 MTS 时每次重启 tile 延迟变化可达约 100 个采样点；开 MTS 后固定为 **−1.00 个采样点（−251 ps）**，MTS 后再做一次训练对齐，每次都是 **−0.002 个采样点（−0.5 ps）**。

　

| ![arch](./docs/img/arch.svg) |
| :--------------------------: |
| **图1** : 设计框图           |

　

## 技术特点

* **不依赖 PYNQ 的时钟**：LMK04828 和两片 LMX2594 通过 PS SPI0 配置，用 PYNQ 的 `LMK04828_500.0` / `LMX2594_500.0` 寄存器组：500 MHz 参考给 tile 2 的 PLL，**PL_CLK 500 MHz 和 SYSREF 5 MHz 以 LVDS 进 PL**（AN11/AP11、AP18/AR18）。tile 2 的 PLL 产生 4 GHz 并分发给其他 tile。
* **四个 RF tile 全部使能**：模拟 SYSREF 依次经过每个 ADC tile，所以 4x2 上没引出的 tile 225 / 227 也要使能。同步 DAC tile 0 / 2 和 ADC tile 0 / 2，参考 tile 为 2。
* **SYSREF 进 fabric**（`rtl/mts_sync.v`）：PL_SYSREF 由 PL_CLK 采样，再打入 500 MHz RF fabric 时钟，驱动 `user_sysref_adc` / `user_sysref_dac`。
* **按 SYSREF 对齐采集**（`rtl/cap_gate.v`）：PS 发出采集请求后，四路 ADC 数据流的采集窗口在下一个 SYSREF 上升沿同时打开，位置在跨时钟 FIFO 之前，保证各路从同一个采样点开始。
* **板上测量**：URAM 播放 4096 点周期 chirp（100 MHz → 1.5 GHz），URAM 采集 64 k 点，±512 点互相关加抛物线插值（4x2 上 ADC_D 极性相反，按相关峰符号处理）。
* **训练对齐**（`sw/src/align.c`）：MTS 对齐的是 tile，不包括模拟通路。MTS 后用 chirp 测延迟，给先到的 ADC 加 RFDC 粗延迟（4 GSPS 下一步 = 一个采样点，扫描验证），再复测，直到两路差在半个采样点以内。
* **状态监测**：串口输出 PL_CLK / SYSREF 频率计、MMCM 锁定、RF tile 状态和 MTS 延迟；fabric 时钟不在时程序拒绝访问 PL。

　

## 上板结果

串口按 `x`：5 次（重启 tile、测量、执行 MTS、测量、对齐）。ADC_D 相对 ADC_B 的延迟，1 个采样点 = 250 ps：

| 次数 | 不开 MTS | 开 MTS | MTS + 对齐 |
| :--: | -------: | -----: | ---------: |
| 1    | +25.530  | −1.002 | **−0.002** |
| 2    | −12.515  | −1.000 | **−0.000** |
| 3    | +11.967  | −1.002 | **−0.002** |
| 4    | +27.918  | −1.002 | **−0.002** |
| 5    | +5.410   | −1.002 | **−0.002** |

MTS 延迟：DAC 120 / 120（偏移 0 / 8），ADC 88 / 88（偏移 0 / 0）。MTS 后剩下的 −1 个采样点每次都一样，训练给 ADC_D 加 1 步粗延迟。粗延迟扫描（按 `d`）：ADC_D 0 / 1 / 2 / 3 步 → −1.001 / −0.001 / +0.999 / +1.999 个采样点；这个配置下 DAC 粗延迟不改变环回延迟，所以补偿放在 ADC 侧。完整日志：[docs/results/board_log.txt](./docs/results/board_log.txt)。

| ![captures](./docs/img/mts_captures.png)                                  |
| :-----------------------------------------------------------------------: |
| **图2** : 不开 MTS、开 MTS、对齐后收到的两路 chirp |

仿真（`sim/run_sim.bat`）：采集窗口在 SYSREF 沿后 9 ns 打开，两路输出完全一致。实现（Vivado 2020.2）：32.4 k LUT（7.6 %）、36.0 k FF、20 URAM，WNS +0.023 ns（[时序](./docs/results/timing_summary.rpt)、[资源](./docs/results/utilization.rpt)）。

　

## 编译和运行

Vivado / Vitis 2020.2，已装 RFSoC 4x2 board files；micro-USB 口提供 JTAG 和 UART1（115200）；两根 SMA 线 DAC_A → ADC_B、DAC_B → ADC_D。

```
vivado -mode batch -source scripts/make_project.tcl -tclargs build   # build/mts_wrapper.xsa
xsct sw/create_vitis.tcl                                             # standalone，mts_jtag.elf
xsct sw/run_jtag.tcl                                                 # JTAG 加载 bitstream + ELF
```

串口按键：`x` 实验，`c` 采集 + 测量，`m` MTS，`a` 训练对齐，`d` 粗延迟扫描，`r` 重启 tile（清除 MTS），`1` / `2` 正弦 / chirp，`p` DAC 开关，`s` 状态，`k` 重新配置时钟（上电后 LMK PLL1 还没稳定时用）。

画采集波形：按 `c` 后运行 `xsct sw/dump_captures.tcl out`，再 `python host/plot_captures.py out`（或给多个目录：不开 MTS / 开 MTS / 对齐后）。

```
rtl/            mts_sync.v, cap_gate.v, clk_meter.v
third_party/    DACRAMstreamer.v, ADCRAMcapture.v（AMD RFSoC-MTS）
constraints/    mts.xdc
scripts/        make_project.tcl（block design、bitstream、.xsa）
sw/src/         main.c, rf_mts.c（RFDC + MTS）, align.c（训练对齐）, capture.c（波形、延迟）, LMK_LMX.c（SPI 时钟）
sim/            tb_mts_sync.v, run_sim.bat
host/           plot_captures.py
```

　

## 引用

如果这个项目对你的研究有帮助，请引用：

```bibtex
@misc{yu2026rfsoc4x2_mts,
    author = {Yijie Yu},
    title = {{RFSoC 4x2 Multi-Tile Synchronization}},
    year = {2026},
    howpublished = {\url{https://github.com/uceeyuf/rfsoc4x2_mts}},
    note = {GitHub repository},
}
```

GitHub 仓库页的 **Cite this repository** 也提供同样的引用（来自 [CITATION.cff](CITATION.cff)）。

　

## 致谢

URAM 播放 / 采集模块和设计思路：[Xilinx/RFSoC-MTS](https://github.com/Xilinx/RFSoC-MTS)（MIT，`third_party/rfsoc_mts/LICENSE`）；时钟寄存器值：PYNQ RFSoC4x2 `LMK04828_500.0` / `LMX2594_500.0`；SPI 驱动来自 [RFSoC4x2_clock_LMK_LMX](https://github.com/uceeyuf/RFSoC4x2_clock_LMK_LMX)；RF 数据转换器驱动：Xilinx `rfdc`。其余文件：BSD 3-Clause，Copyright (c) 2026, Yijie Yu。
