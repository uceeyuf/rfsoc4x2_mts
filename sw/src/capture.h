/*
 * DAC waveform player, SYSREF-aligned ADC capture and alignment measurement.
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#ifndef CAPTURE_H_
#define CAPTURE_H_

#include "xil_types.h"

#define GPIO_BASE       0xA0040000U
#define DAC_MEM_BASE    0xA0100000U
#define CAP_MEM_BASE    0xA0200000U     /* channel i at CAP_MEM_BASE + i * 0x80000 */
#define NSAMP           65536
#define NCH             4
#define FS_HZ           4.0e9

extern const char *ch_name[NCH];

int  pl_clock_start(void);              /* measure PL_CLK, reset the MMCM, wait for lock and SYSREF */
int  pl_clock_ok(void);                 /* MMCM locked: PL memories may be accessed */
void dac_play(int on);
void wave_sine(double f_hz);
void wave_chirp(double f0_hz, double f1_hz);
int  capture(void);                     /* 0 = new samples in all four channels */
/* print level per channel and the delay of every active channel vs. the reference one */
void analyze(double *lag_out);          /* lag_out: delay of ADC_D vs ADC_B in samples */
const s16 *capture_data(int ch);

#endif
