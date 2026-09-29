/*
 * DAC waveform player, SYSREF-aligned ADC capture and alignment measurement.
 *
 * GPIO (0xA0040000) ch1: [0] DAC play, [1] capture request, [2] capture arm, [3] MMCM reset
 *                   ch2: [0] MMCM locked, [1] toggles on every SYSREF edge
 * Memories: DAC player 0xA0100000 (64 k samples each for DAC_A = I and DAC_B = Q, in blocks
 * of 8 I then 8 Q), captures 0xA0200000 + i * 0x80000, 64 k int16 each.
 *
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#include <math.h>
#include <stdio.h>
#include "xil_io.h"
#include "xil_printf.h"
#include "sleep.h"
#include "capture.h"

#define GPIO_DATA       (GPIO_BASE + 0x0)
#define GPIO_TRI        (GPIO_BASE + 0x4)
#define GPIO_DATA2      (GPIO_BASE + 0x8)
#define BIT_PLAY        0x1
#define BIT_CAP_REQ     0x2
#define BIT_CAP_ARM     0x4
#define BIT_MMCM_RST    0x8

/* cap0 = m00 (tile 224 ch "01"), cap1 = m02, cap2 = m20 (tile 226), cap3 = m22 */
const char *ch_name[NCH] = {"ADC_D", "ADC_C", "ADC_B", "ADC_A"};

static s16 buf[NCH][NSAMP];
static u32 gpio_out;

static void gpio_write(u32 v) { gpio_out = v; Xil_Out32(GPIO_DATA, v); }
static void gpio_set(u32 m, int on) { gpio_write(on ? (gpio_out | m) : (gpio_out & ~m)); }

#define METER_CLK       (GPIO_BASE + 0x1000)    /* PL_CLK cycle count, Gray */
#define METER_SYSREF    (GPIO_BASE + 0x1008)    /* SYSREF edge count, Gray */

static u32 gray2bin(u32 g)
{
    for (u32 s = 1; s < 32; s <<= 1)
        g ^= g >> s;
    return g;
}

/* PL_CLK and SYSREF frequency from the free-running counters (100 ms gate) */
static void pl_clock_measure(void)
{
    u32 c0 = gray2bin(Xil_In32(METER_CLK)), s0 = gray2bin(Xil_In32(METER_SYSREF));
    usleep(100000);
    u32 c1 = gray2bin(Xil_In32(METER_CLK)), s1 = gray2bin(Xil_In32(METER_SYSREF));
    u32 fclk_khz = (c1 - c0) / 100;             /* counts per 100 ms -> kHz */
    u32 fsys_hz = (s1 - s0) * 10;
    xil_printf("PL_CLK %d.%03d MHz, SYSREF %d.%03d MHz\r\n", fclk_khz / 1000, fclk_khz % 1000,
               fsys_hz / 1000000, (fsys_hz / 1000) % 1000);
}

static int pl_ok;
int pl_clock_ok(void) { return pl_ok; }

int pl_clock_start(void)
{
    pl_ok = 0;
    pl_clock_measure();
    Xil_Out32(GPIO_TRI, 0);
    gpio_write(BIT_MMCM_RST);
    usleep(1000);
    gpio_write(0);
    for (int i = 0; i < 100 && !(Xil_In32(GPIO_DATA2) & 1); i++)
        usleep(1000);
    if (!(Xil_In32(GPIO_DATA2) & 1)) {
        xil_printf("PL clock: MMCM not locked (no PL_CLK from the LMK04828?)\r\n");
        return -1;
    }
    u32 s0 = Xil_In32(GPIO_DATA2) & 2;
    int toggles = 0;
    for (int i = 0; i < 1000; i++) {
        u32 s = Xil_In32(GPIO_DATA2) & 2;
        if (s != s0) { toggles++; s0 = s; }
        usleep(10);
    }
    xil_printf("PL clock: MMCM locked, SYSREF %s\r\n", toggles ? "present" : "MISSING");
    pl_ok = 1;
    return toggles ? 0 : -1;
}

void dac_play(int on) { gpio_set(BIT_PLAY, on); }

void dac_write_iq(const s16 *i, const s16 *q)
{
    /* 32-byte blocks: 8 samples for DAC_A (I), then 8 for DAC_B (Q) */
    for (int n = 0; n < NSAMP; n += 8) {
        UINTPTR a = DAC_MEM_BASE + n * 4;
        for (int j = 0; j < 8; j += 2) {
            Xil_Out32(a + j * 2, (u16)i[n + j] | ((u32)(u16)i[n + j + 1] << 16));
            Xil_Out32(a + 16 + j * 2, (u16)q[n + j] | ((u32)(u16)q[n + j + 1] << 16));
        }
    }
}

static void dac_write(const s16 *w) { dac_write_iq(w, w); }

void wave_sine(double f_hz)
{
    /* snap to a bin so the 64 k sample buffer repeats without a phase jump */
    int bin = (int)(f_hz / FS_HZ * NSAMP + 0.5);
    for (int i = 0; i < NSAMP; i++)
        buf[0][i] = (s16)(16000.0 * sin(2.0 * M_PI * bin * i / NSAMP));
    dac_write(buf[0]);
    xil_printf("DAC waveform: sine %d MHz, half scale\r\n", (int)(bin * FS_HZ / NSAMP / 1e6 + 0.5));
}

void wave_chirp(double f0_hz, double f1_hz)
{
    /*
     * linear up-chirp repeated every CHIRP_LEN samples: the 8192-sample correlation window
     * holds two full sweeps, so the peak is sharp and unique within +/- CHIRP_LEN / 2
     */
    enum { CHIRP_LEN = 4096 };
    double k = (f1_hz - f0_hz) / (CHIRP_LEN / FS_HZ);
    for (int i = 0; i < NSAMP; i++) {
        double t = (i % CHIRP_LEN) / FS_HZ;
        buf[0][i] = (s16)(16000.0 * sin(2.0 * M_PI * (f0_hz * t + 0.5 * k * t * t)));
    }
    dac_write(buf[0]);
    xil_printf("DAC waveform: chirp %d -> %d MHz every %d samples, half scale\r\n",
               (int)(f0_hz / 1e6), (int)(f1_hz / 1e6), CHIRP_LEN);
}

int capture(void)
{
    gpio_set(BIT_CAP_ARM, 0);
    usleep(10);
    gpio_set(BIT_CAP_ARM, 1);           /* writers restart at address 0 */
    usleep(10);
    gpio_set(BIT_CAP_REQ, 1);           /* window opens on the next SYSREF edge */
    usleep(2000);
    gpio_set(BIT_CAP_REQ, 0);
    gpio_set(BIT_CAP_ARM, 0);

    for (int c = 0; c < NCH; c++) {
        UINTPTR base = CAP_MEM_BASE + c * 0x80000;
        for (int i = 0; i < NSAMP; i += 2) {
            u32 v = Xil_In32(base + i * 2);
            buf[c][i] = (s16)(v & 0xFFFF);
            buf[c][i + 1] = (s16)(v >> 16);
        }
    }
    return 0;
}

const s16 *capture_data(int ch) { return buf[ch]; }

static double rms(const s16 *x)
{
    double s = 0;
    for (int i = 0; i < NSAMP; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / NSAMP);
}

/* delay of y relative to x in samples (positive: y later), sign of the correlation peak */
static double xcorr_lag(const s16 *x, const s16 *y, int *inverted)
{
    enum { N = 8192, L = 512, S = 4096 };   /* +/-128 ns, well inside the 4096-sample chirp period */
    static double r[2 * L + 1];
    int best = 0;
    for (int k = -L; k <= L; k++) {
        double acc = 0;
        for (int i = 0; i < N; i++)
            acc += (double)x[S + i] * y[S + i + k];
        r[k + L] = acc;
        if (fabs(acc) > fabs(r[best]))
            best = k + L;
    }
    *inverted = r[best] < 0;
    double frac = 0;
    if (best > 0 && best < 2 * L) {
        double a = fabs(r[best - 1]), b = fabs(r[best]), c = fabs(r[best + 1]);
        double d = a - 2 * b + c;
        if (d != 0)
            frac = 0.5 * (a - c) / d;
    }
    return best - L + frac;
}

void analyze(double *lag_out)
{
    double lvl[NCH];
    int ref = -1;
    xil_printf("level:");
    for (int c = 0; c < NCH; c++) {
        lvl[c] = rms(buf[c]);
        printf("  %s %6.0f", ch_name[c], lvl[c]);
    }
    printf("  (rms, full scale 32767)\r\n");

    /* reference: ADC_B if it has a signal, otherwise the first active channel */
    if (lvl[2] > 300) ref = 2;
    for (int c = 0; c < NCH && ref < 0; c++)
        if (lvl[c] > 300) ref = c;
    if (ref < 0) {
        xil_printf("no signal on any ADC input\r\n");
        return;
    }
    for (int c = 0; c < NCH; c++) {
        if (c == ref || lvl[c] <= 300)
            continue;
        int inv;
        double lag = xcorr_lag(buf[ref], buf[c], &inv);
        printf("delay %s vs %s: %+8.3f samples = %+8.1f ps%s\r\n", ch_name[c], ch_name[ref],
               lag, lag * 250.0, inv ? "  (inverted polarity)" : "");
        if (lag_out && c == 0 && ref == 2)
            *lag_out = lag;
    }
}

double measure_lag(void)
{
    int inv;
    capture();
    if (rms(buf[0]) < 300 || rms(buf[2]) < 300)
        return NAN;
    return xcorr_lag(buf[2], buf[0], &inv);
}
