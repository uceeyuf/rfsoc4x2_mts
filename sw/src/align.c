/*
 * Sample alignment after MTS by training on the loopback chirp.
 *
 * MTS lines up the tiles, not the analog paths: cables, board traces and the converter
 * pipelines still leave a fixed offset between the two loopbacks (about one sample here).
 * Training measures the delay of ADC_D vs ADC_B on the chirp, delays the earlier path with
 * the RFDC coarse delay and measures again until both are within half a sample.
 *
 * The loopback only sees the sum DAC + ADC, so the correction goes on the receive side:
 * ADC coarse delay, one step = one sample at 4 GSPS (measured with align_sweep(); the DAC
 * coarse delay did not move the loopback delay in this configuration).
 *
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#include <math.h>
#include <stdio.h>
#include "xrfdc.h"
#include "xil_printf.h"
#include "sleep.h"
#include "rf_mts.h"
#include "capture.h"
#include "align.h"

#ifndef ALIGN_TYPE
#define ALIGN_TYPE      XRFDC_ADC_TILE
#endif
#define MAX_STEPS       40              /* Gen3 coarse delay range */

/* tile / block of the converters in the two loopbacks: [0] ADC_B path, [1] ADC_D path */
static const u32 adc_tile[2] = {2, 0};  /* ADC_B tile 226, ADC_D tile 224, block 0 */
static const u32 dac_tile[2] = {2, 0};  /* DAC_A tile 230, DAC_B tile 228, block 0 */

static int steps[2];
static double step_samples = 1.0;       /* samples per coarse delay step, measured by training */
static double residual = NAN;

static int apply(u32 type, int path, int n)
{
    u32 tile = (type == XRFDC_ADC_TILE) ? adc_tile[path] : dac_tile[path];
    return rf_coarse_delay(type, tile, 0, n);
}

static double settle_and_measure(void)
{
    usleep(20000);
    return measure_lag();
}

void align_clear(void)
{
    for (int p = 0; p < 2; p++) {
        apply(XRFDC_ADC_TILE, p, 0);
        apply(XRFDC_DAC_TILE, p, 0);
        steps[p] = 0;
    }
    residual = NAN;
}

double align_residual(void) { return residual; }

void align_sweep(void)
{
    static const char *side[2] = {"ADC_D (tile 224)", "DAC_B (tile 228)"};
    static const u32 type[2] = {XRFDC_ADC_TILE, XRFDC_DAC_TILE};
    align_clear();
    xil_printf("coarse delay sweep, delay ADC_D vs ADC_B in samples\r\n");
    for (int s = 0; s < 2; s++) {
        char line[160];
        int k = snprintf(line, sizeof(line), "  %s:", side[s]);
        for (int n = 0; n <= 3; n++) {
            if (apply(type[s], 1, n))
                k += snprintf(line + k, sizeof(line) - k, "  %d: failed", n);
            else
                k += snprintf(line + k, sizeof(line) - k, "  %d: %+7.3f", n, settle_and_measure());
        }
        apply(type[s], 1, 0);
        printf("%s\r\n", line);
    }
}

int align_train(void)
{
    align_clear();
    double lag = settle_and_measure();
    if (isnan(lag)) {
        xil_printf("align: no signal on ADC_B / ADC_D\r\n");
        return -1;
    }
    printf("align: start %+.3f samples\r\n", lag);

    /* learn the step size once: one step on the earlier path */
    int early = lag < 0 ? 1 : 0;        /* negative: ADC_D early */
    if (fabs(lag) >= 0.5) {
        apply(ALIGN_TYPE, early, 1);
        double l1 = settle_and_measure();
        step_samples = fabs(l1 - lag);
        apply(ALIGN_TYPE, early, 0);
        printf("align: one coarse delay step = %.3f samples\r\n", step_samples);
        if (step_samples < 0.5) {
            xil_printf("align: coarse delay has no effect\r\n");
            return -1;
        }
    }

    for (int it = 0; it < 4 && fabs(lag) >= 0.5; it++) {
        int n = (int)lround(fabs(lag) / step_samples);
        if (n == 0)
            break;
        /* a positive lag delays the ADC_B path, a negative one the ADC_D path; keep the other at 0 */
        int p = lag < 0 ? 1 : 0;
        steps[p] += n;
        if (steps[0] && steps[1]) {
            int m = steps[0] < steps[1] ? steps[0] : steps[1];
            steps[0] -= m;
            steps[1] -= m;
        }
        if (steps[0] > MAX_STEPS || steps[1] > MAX_STEPS) {
            xil_printf("align: offset beyond the coarse delay range\r\n");
            return -1;
        }
        apply(ALIGN_TYPE, 0, steps[0]);
        apply(ALIGN_TYPE, 1, steps[1]);
        lag = settle_and_measure();
    }
    residual = lag;
    printf("align: %s delay ADC_B path %d, ADC_D path %d steps -> %+.3f samples (%+.1f ps)\r\n",
           ALIGN_TYPE == XRFDC_ADC_TILE ? "ADC" : "DAC", steps[0], steps[1], lag, lag * 250.0);
    return fabs(lag) < 0.5 ? 0 : -1;
}
