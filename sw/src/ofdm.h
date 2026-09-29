/*
 * I/Q OFDM over the two DACs: DAC_A = I, DAC_B = Q (baseband for an external I/Q modulator).
 * Board loopback: ADC_B receives I, ADC_D receives Q. Same frame and receiver as host/ofdm.py.
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#ifndef OFDM_H_
#define OFDM_H_

typedef struct {
    int    frame_pos;           /* start of the first training body in the capture */
    int    q_sign, q_lag;       /* Q rail polarity and offset vs I (samples) */
    double irr_db;              /* image rejection A / B, median over the active sub-carriers */
    double evm_lin_db, evm_wl_db;
    int    err_lin, err_wl, bits;
} ofdm_result;

void   ofdm_tx(int bits_per_sym);           /* 2, 4, 6, 8: QPSK .. 256-QAM, writes the DAC buffer */
int    ofdm_rx(ofdm_result *r);             /* capture + demodulate one frame, 0 = frame found */
void   ofdm_print(const ofdm_result *r);
double ofdm_rate_gbps(int bits_per_sym);
int    ofdm_bits_per_sym(void);
void   ofdm_set_level(double rms_fs);           /* RMS per rail as a fraction of DAC full scale */
double ofdm_level(void);

#endif
