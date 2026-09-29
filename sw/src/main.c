/*
 * RFSoC4x2 multi-tile synchronization, bare metal over JTAG (no PYNQ).
 *
 * Wiring (two SMA cables): DAC_A -> ADC_B, DAC_B -> ADC_D.
 * DAC_A / DAC_B sit on DAC tiles 230 / 228, ADC_B / ADC_D on ADC tiles 226 / 224, so the
 * measured delay ADC_D vs ADC_B contains the DAC and the ADC tile skew. Without MTS it
 * changes whenever the tiles restart; with MTS it stays at the cable difference.
 *
 * UART1 115200: see help().
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#include <stdio.h>
#include "xparameters.h"
#include "xil_printf.h"
#include "xuartps_hw.h"
#include "sleep.h"
#include "LMK_LMX.h"
#include "rf_mts.h"
#include "capture.h"
#include "align.h"
#include "ofdm.h"

static int playing;

static void help(void)
{
    xil_printf("\r\nkeys: 1 sine 250 MHz | 2 chirp 100 MHz -> 1.5 GHz | p DAC play on/off\r\n"
               "      c capture + measure | m run MTS | r restart tiles (clears MTS)\r\n"
               "      a align by training (after MTS) | d coarse delay sweep\r\n"
               "      o OFDM: next modulation (QPSK / 16 / 64 / 256-QAM), I -> DAC_A, Q -> DAC_B\r\n"
               "      e OFDM receive | f OFDM experiment: without MTS, MTS, MTS + align\r\n"
               "      l OFDM level +0.05 FS | L OFDM EVM vs level sweep\r\n"
               "      x experiment: 5 x (restart, measure, MTS, measure, align) | s status | h help\r\n"
               "      k reprogram the clock chips (if LMK PLL1 was still settling at power-on)\r\n"
               "wiring: DAC_A -> ADC_B, DAC_B -> ADC_D\r\n\r\n");
}

static void clocks(void)
{
    xil_printf("Programming LMK04828 / LMX2594 (500 MHz) over SPI0...\r\n");
    write_clk(0);
    write_clk(1);
    write_clk(2);
    sleep(1);
    pl_clock_start();
}

static void measure(double *lag)
{
    capture();
    analyze(lag);
}

static void ofdm_experiment(void)
{
    static const char *cond[3] = {"without MTS", "MTS", "MTS + align"};
    ofdm_result r[3];
    int m = ofdm_bits_per_sym();
    for (int i = 0; i < 3; i++) {
        if (i == 0) {
            rf_reset_tiles();
            align_clear();
        } else if (i == 1) {
            rf_mts();
        } else {
            wave_chirp(100e6, 1500e6);      /* training needs the chirp on both rails */
            align_train();
        }
        ofdm_tx(m);
        usleep(100000);
        ofdm_rx(&r[i]);
        ofdm_print(&r[i]);
    }
    printf("\r\n%d-QAM, %.2f Gb/s | Q lag | image rej. | linear EQ EVM / errors | widely linear EQ EVM / errors\r\n",
           1 << m, ofdm_rate_gbps(m));
    for (int i = 0; i < 3; i++)
        printf("%-12s |  %+4d | %7.1f dB | %6.1f dB %7d        | %6.1f dB %7d\r\n", cond[i], r[i].q_lag,
               r[i].irr_db, r[i].evm_lin_db, r[i].err_lin, r[i].evm_wl_db, r[i].err_wl);
    printf("(%d bits per frame)\r\n", r[0].bits);
}

/* EVM vs transmit level for the current modulation: noise at low level, distortion at high */
static void level_sweep(void)
{
    static const double lv[] = {0.05, 0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40};
    enum { NL = sizeof(lv) / sizeof(lv[0]) };
    ofdm_result r[NL];
    double keep = ofdm_level();
    int m = ofdm_bits_per_sym();
    for (int i = 0; i < NL; i++) {
        ofdm_set_level(lv[i]);
        ofdm_tx(m);
        usleep(100000);
        ofdm_rx(&r[i]);
    }
    printf("\r\n%d-QAM | RMS per rail (FS) | linear EQ EVM / errors | widely linear EQ EVM / errors\r\n", 1 << m);
    for (int i = 0; i < NL; i++)
        printf("        | %.2f              | %6.1f dB %7d        | %6.1f dB %7d\r\n",
               lv[i], r[i].evm_lin_db, r[i].err_lin, r[i].evm_wl_db, r[i].err_wl);
    ofdm_set_level(keep);
    ofdm_tx(m);
}

static void experiment(void)
{
    double before[5], after[5], aligned[5];
    for (int i = 0; i < 5; i++) {
        before[i] = after[i] = 1e9;
        rf_reset_tiles();
        align_clear();
        usleep(200000);
        measure(&before[i]);
        rf_mts();
        usleep(200000);
        measure(&after[i]);
        align_train();
        aligned[i] = align_residual();
    }
    xil_printf("\r\nrun | ADC_D vs ADC_B without MTS | with MTS   | MTS + align  (samples, 1 sample = 250 ps)\r\n");
    for (int i = 0; i < 5; i++)
        printf(" %d  | %+10.3f                 | %+10.3f | %+10.3f\r\n", i + 1, before[i], after[i], aligned[i]);
}

int main(void)
{
    xil_printf("\r\n==== RFSoC4x2 multi-tile sync: DAC 228/230 -> ADC 224/226, 4.0 GSPS ====\r\n");
    clocks();
    if (rf_init())
        xil_printf("RF tiles not ready - LMK PLL1 may still be settling, press 'k'\r\n");
    if (pl_clock_ok()) {
        wave_chirp(100e6, 1500e6);
        dac_play(playing = 1);
    }
    help();

    for (;;) {
        if (!XUartPs_IsReceiveData(STDIN_BASEADDRESS))
            continue;
        char key = XUartPs_ReadReg(STDIN_BASEADDRESS, XUARTPS_FIFO_OFFSET);
        /* without the fabric clock an access to the PL memories would never complete */
        if (!pl_clock_ok() && key != 'k' && key != 's' && key != 'h') {
            xil_printf("PL clock not running (MMCM unlocked): only k / s / h\r\n");
            continue;
        }
        switch (key) {
        case '1': wave_sine(250e6); break;
        case '2': wave_chirp(100e6, 1500e6); break;
        case 'p': dac_play(playing = !playing); xil_printf("DAC play %s\r\n", playing ? "on" : "off"); break;
        case 'c': measure(0); break;
        case 'm': rf_mts(); break;
        case 'r': rf_reset_tiles(); align_clear(); break;
        case 'a': align_train(); break;
        case 'd': align_sweep(); break;
        case 'o': {
            static const int mods[4] = {2, 4, 6, 8};
            static int mi = 0;
            mi = (mi + 1) % 4;
            ofdm_tx(mods[mi]);
            break;
        }
        case 'e': {
            ofdm_result r;
            ofdm_rx(&r);
            ofdm_print(&r);
            break;
        }
        case 'f': ofdm_experiment(); break;
        case 'l': {
            double lv = ofdm_level() + 0.05;
            ofdm_set_level(lv > 0.401 ? 0.10 : lv);
            ofdm_tx(ofdm_bits_per_sym());
            break;
        }
        case 'L': level_sweep(); break;
        case 'x': experiment(); break;
        case 's': rf_status(); break;
        case 'k':
            clocks();
            rf_init();
            if (pl_clock_ok()) {
                wave_chirp(100e6, 1500e6);
                dac_play(playing = 1);
            }
            break;
        case 'h': help(); break;
        default: break;
        }
    }
    return 0;
}
