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

static int playing;

static void help(void)
{
    xil_printf("\r\nkeys: 1 sine 250 MHz | 2 chirp 100 MHz -> 1.5 GHz | p DAC play on/off\r\n"
               "      c capture + measure | m run MTS | r restart tiles (clears MTS)\r\n"
               "      x experiment: 5 x (restart, capture, MTS, capture) | s status | h help\r\n"
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

static void experiment(void)
{
    double before[5], after[5];
    for (int i = 0; i < 5; i++) {
        before[i] = after[i] = 1e9;
        rf_reset_tiles();
        usleep(200000);
        measure(&before[i]);
        rf_mts();
        usleep(200000);
        measure(&after[i]);
    }
    xil_printf("\r\nrun | ADC_D vs ADC_B without MTS | with MTS  (samples, 1 sample = 250 ps)\r\n");
    for (int i = 0; i < 5; i++)
        printf(" %d  | %+10.3f                 | %+10.3f\r\n", i + 1, before[i], after[i]);
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
        case 'r': rf_reset_tiles(); break;
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
