/*
 * I/Q OFDM over the two DACs: DAC_A = I, DAC_B = Q (baseband for an external I/Q modulator).
 *
 * 4.0 GSPS, N = 1024, CP = 128 (3.9 MHz sub-carriers), active k = +/-8 .. +/-250
 * (31 .. 977 MHz), pilots on k = +/-16, 32, ... Frame: two training symbols, 26 data
 * symbols, 512 zeros = 32768 samples; the 64 k DAC buffer holds two identical frames, so any
 * 64 k capture contains a whole one.
 *
 * I and Q take separate converter paths, so the receiver has two equalizers: a linear one
 * (Y = H X per sub-carrier) and a widely linear one (Y(k) = A(k) X(k) + B(k) X*(-k), from
 * two training symbols that differ in the sign of k < 0). The second also removes the I/Q
 * image left by gain, frequency response or delay differences between the two paths.
 *
 * Same generators, frame and receiver as host/ofdm.py.
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#pragma GCC optimize("O2")

#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xil_printf.h"
#include "capture.h"
#include "ofdm.h"

#define N           1024
#define CP          128
#define SYM         (N + CP)
#define K_LO        8           /* the board baluns roll off below ~30 MHz */
#define K_HI        250
#define PILOT_STEP  16
#define N_DATA_SYM  26
#define FRAME       32768
#define RMS_FS      0.25        /* default RMS per rail, fraction of DAC full scale */
#define BACKOFF     8
#define SMOOTH      3           /* channel estimates averaged over +/-SMOOTH sub-carriers */
#define MAX_DATA    (2 * (K_HI - K_LO + 1))

typedef float complex cf;

static int n_data, data_k[MAX_DATA];
static int n_pilot, pilot_k[MAX_DATA];
static cf  t1[N], t2[N];
static cf  tw[N / 2];
static int ready, cur_m = 4;
static double rms_fs = RMS_FS;

static cf  frame[FRAME];
static s16 rail_i[NSAMP], rail_q[NSAMP];
static cf  Y[2 + N_DATA_SYM][N];
static float ai[NSAMP], aq[NSAMP];

static u32 xs_state;
static u32 xs_next(void)
{
    u32 s = xs_state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return xs_state = s;
}

static int kidx(int k) { return k < 0 ? k + N : k; }
static float pilot_value(int k) { return (((k < 0 ? k - PILOT_STEP + 1 : k) / PILOT_STEP) & 1) ? -1.0f : 1.0f; }

/* radix-2 FFT in place, sign -1 forward, +1 inverse (unscaled) */
static void fft(cf *x, int sign)
{
    for (int i = 1, j = 0; i < N; i++) {
        int b = N >> 1;
        for (; j & b; b >>= 1)
            j ^= b;
        j |= b;
        if (i < j) {
            cf t = x[i]; x[i] = x[j]; x[j] = t;
        }
    }
    for (int len = 2; len <= N; len <<= 1) {
        int step = N / len;
        for (int i = 0; i < N; i += len)
            for (int k = 0; k < len / 2; k++) {
                cf w = sign < 0 ? tw[k * step] : conjf(tw[k * step]);
                cf u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
            }
    }
}

static void init(void)
{
    if (ready)
        return;
    for (int k = 0; k < N / 2; k++)
        tw[k] = cexpf(-2.0f * (float)M_PI * I * k / N);
    n_data = n_pilot = 0;
    for (int k = -K_HI; k <= K_HI; k++) {
        if (k > -K_LO && k < K_LO)
            continue;
        if (k % PILOT_STEP == 0)
            pilot_k[n_pilot++] = k;
        else
            data_k[n_data++] = k;
    }
    xs_state = 0x1234ABCD;
    memset(t1, 0, sizeof(t1));
    for (int k = -K_HI; k <= K_HI; k++) {
        if (k > -K_LO && k < K_LO)
            continue;
        u32 r = xs_next();
        t1[kidx(k)] = ((r & 1 ? 1.0f : -1.0f) + I * (r & 2 ? 1.0f : -1.0f)) / sqrtf(2.0f);
    }
    for (int k = 0; k < N; k++)
        t2[k] = (k > N / 2) ? -t1[k] : t1[k];
    ready = 1;
}

/* payload: xorshift32 words, LSB first */
static u32 bit_word;
static int bit_pos;
static void bits_reset(int m) { xs_state = 0xC0FFEE00u + m; bit_pos = 32; }
static int bit_next(void)
{
    if (bit_pos == 32) {
        bit_word = xs_next();
        bit_pos = 0;
    }
    return (bit_word >> bit_pos++) & 1;
}

static float qam_scale(int m) { int L = 1 << (m / 2); return sqrtf(2.0f * (L * L - 1) / 3.0f); }

static float axis_map(int h)
{
    int v = 0;
    for (int j = 0; j < h; j++)
        v = (v << 1) | (bit_next() ^ (v & 1));   /* Gray -> binary */
    return (float)(2 * v - ((1 << h) - 1));
}

static cf qam_next(int m)
{
    float s = qam_scale(m);
    float re = axis_map(m / 2);
    float im = axis_map(m / 2);
    return (re + I * im) / s;
}

/* bit errors of one received symbol against the next reference bits */
static int axis_errors(float v, int h, float s)
{
    int L = 1 << h;
    int idx = (int)lroundf((v * s + (L - 1)) / 2);
    idx = idx < 0 ? 0 : idx > L - 1 ? L - 1 : idx;
    int g = idx ^ (idx >> 1), e = 0;
    for (int j = h - 1; j >= 0; j--)
        e += ((g >> j) & 1) != bit_next();
    return e;
}

int    ofdm_bits_per_sym(void) { return cur_m; }
void   ofdm_set_level(double fs) { rms_fs = fs; }
double ofdm_level(void) { return rms_fs; }
double ofdm_rate_gbps(int m) { init(); return (double)N_DATA_SYM * n_data * m / (FRAME / FS_HZ) / 1e9; }

void ofdm_tx(int m)
{
    static cf f[N];
    init();
    cur_m = m;
    memset(frame, 0, sizeof(frame));
    bits_reset(m);
    for (int s = 0; s < 2 + N_DATA_SYM; s++) {
        if (s == 0)
            memcpy(f, t1, sizeof(f));
        else if (s == 1)
            memcpy(f, t2, sizeof(f));
        else {
            memset(f, 0, sizeof(f));
            for (int i = 0; i < n_data; i++)
                f[kidx(data_k[i])] = qam_next(m);
            for (int i = 0; i < n_pilot; i++)
                f[kidx(pilot_k[i])] = pilot_value(pilot_k[i]);
        }
        fft(f, +1);
        cf *o = &frame[s * SYM];
        memcpy(o, &f[N - CP], CP * sizeof(cf));
        memcpy(o + CP, f, N * sizeof(cf));
    }
    double p = 0;
    for (int n = 0; n < (2 + N_DATA_SYM) * SYM; n++)
        p += crealf(frame[n]) * crealf(frame[n]);
    float g = (float)(rms_fs * 32767 / sqrt(p / ((2 + N_DATA_SYM) * SYM)));
    int clip = 0;
    for (int n = 0; n < NSAMP; n++) {
        cf v = frame[n % FRAME] * g;
        float re = crealf(v), im = cimagf(v);
        if (fabsf(re) > 32767 || fabsf(im) > 32767)
            clip++;
        rail_i[n] = (s16)fmaxf(-32767, fminf(32767, lroundf(re)));
        rail_q[n] = (s16)fmaxf(-32767, fminf(32767, lroundf(im)));
    }
    dac_write_iq(rail_i, rail_q);
    printf("OFDM TX: %d-QAM, %d data + %d pilot sub-carriers, %d symbols, %.2f Gb/s, "
           "I -> DAC_A, Q -> DAC_B, RMS %.2f FS per rail, %d clipped\r\n",
           1 << m, n_data, n_pilot, N_DATA_SYM, ofdm_rate_gbps(m), rms_fs, clip);
}

static float dotf(const float *a, const float *b, int n)
{
    float s = 0;
    for (int i = 0; i < n; i++)
        s += a[i] * b[i];
    return s;
}

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* the channel is smooth over 3.9 MHz: average an estimate over neighbouring sub-carriers,
   each side of DC, after removing the phase slope of the window back-off */
static void smooth(cf *arr)
{
    enum { NK = K_HI - K_LO + 1 };
    static cf v[NK], ph[NK];
    for (int sgn = 1; sgn >= -1; sgn -= 2) {
        for (int i = 0; i < NK; i++) {
            int k = sgn * (K_LO + i);
            ph[i] = cexpf(-2.0f * (float)M_PI * I * k * BACKOFF / N);
            v[i] = arr[kidx(k)] / ph[i];
        }
        for (int i = 0; i < NK; i++) {
            int lo = i - SMOOTH < 0 ? 0 : i - SMOOTH, hi = i + SMOOTH > NK - 1 ? NK - 1 : i + SMOOTH;
            cf s = 0;
            for (int j = lo; j <= hi; j++)
                s += v[j];
            arr[kidx(sgn * (K_LO + i))] = s / (float)(hi - lo + 1) * ph[i];
        }
    }
}

int ofdm_rx(ofdm_result *r)
{
    static float rr[N], ri[N], irr[MAX_DATA + 64];
    static cf buf[N], H[N], A[N], B[N], X[N];
    init();
    memset(r, 0, sizeof(*r));
    capture();
    const s16 *ci = capture_data(2), *cq = capture_data(0);   /* ADC_B = I, ADC_D = Q */
    for (int n = 0; n < NSAMP; n++) {
        ai[n] = ci[n];
        aq[n] = cq[n];
    }

    /* timing on the I rail against Re{training} */
    memcpy(buf, t1, sizeof(buf));
    fft(buf, +1);
    for (int n = 0; n < N; n++) {
        rr[n] = crealf(buf[n]);
        ri[n] = cimagf(buf[n]);
    }
    float best = 0;
    int p = 0;
    for (int l = 0; l < FRAME; l++) {
        float c = fabsf(dotf(&ai[l], rr, N));
        if (c > best) {
            best = c;
            p = l;
        }
    }
    /* Q rail: polarity and offset against Im{training} */
    float bq = 0, cqv = 0;
    for (int l = -CP; l <= CP; l++) {
        if (p + l < 0 || p + l + N > NSAMP)
            continue;
        float c = dotf(&aq[p + l], ri, N);
        if (fabsf(c) > bq) {
            bq = fabsf(c);
            cqv = c;
            r->q_lag = l;
        }
    }
    r->frame_pos = p;
    r->q_sign = cqv < 0 ? -1 : 1;

    int start = p - CP - BACKOFF;
    if (start < 0)
        start += FRAME;
    for (int s = 0; s < 2 + N_DATA_SYM; s++) {
        int o = start + s * SYM + CP;
        for (int n = 0; n < N; n++)
            Y[s][n] = (ai[o + n] + I * r->q_sign * aq[o + n]) / N;
        fft(Y[s], -1);
    }

    /* channel: linear H, widely linear A / B */
    int ni = 0;
    for (int k = K_LO; k <= K_HI; k++) {
        int kp = k, kn = N - k;
        H[kp] = 0.5f * (Y[0][kp] / t1[kp] + Y[1][kp] / t2[kp]);
        H[kn] = 0.5f * (Y[0][kn] / t1[kn] + Y[1][kn] / t2[kn]);
        A[kp] = (Y[0][kp] + Y[1][kp]) / (2 * t1[kp]);
        B[kp] = (Y[0][kp] - Y[1][kp]) / (2 * conjf(t1[kn]));
        A[kn] = (Y[0][kn] - Y[1][kn]) / (2 * t1[kn]);
        B[kn] = (Y[0][kn] + Y[1][kn]) / (2 * conjf(t1[kp]));
    }
    if (abs(r->q_lag) <= 2) {   /* a large I/Q offset makes A and B rotate from one sub-carrier to the next */
        smooth(H);
        smooth(A);
        smooth(B);
    }
    for (int k = K_LO; k <= K_HI; k++) {
        int kp = k, kn = N - k;
        irr[ni++] = 10 * log10f(cabsf(A[kp]) * cabsf(A[kp]) / fmaxf(cabsf(B[kp]) * cabsf(B[kp]), 1e-30f));
        irr[ni++] = 10 * log10f(cabsf(A[kn]) * cabsf(A[kn]) / fmaxf(cabsf(B[kn]) * cabsf(B[kn]), 1e-30f));
    }
    qsort(irr, ni, sizeof(float), cmpf);
    r->irr_db = 0.5 * (irr[ni / 2 - 1] + irr[ni / 2]);

    int m = cur_m;
    float sc = qam_scale(m);
    for (int wl = 0; wl < 2; wl++) {
        double e2 = 0, s2 = 0;
        int errs = 0;
        bits_reset(m);
        /* reference symbols and bits come from the same generator: draw the symbol, then
           re-draw its bits for the error count (the generator is replayed per symbol) */
        for (int s = 0; s < N_DATA_SYM; s++) {
            memset(X, 0, sizeof(X));
            for (int k = K_LO; k <= K_HI; k++) {
                int kp = k, kn = N - k;
                if (!wl) {
                    X[kp] = Y[2 + s][kp] / H[kp];
                    X[kn] = Y[2 + s][kn] / H[kn];
                } else {
                    cf a = A[kp], b = B[kp], c = conjf(B[kn]), d = conjf(A[kn]);
                    cf y1 = Y[2 + s][kp], y2 = conjf(Y[2 + s][kn]);
                    cf det = a * d - b * c;
                    X[kp] = (d * y1 - b * y2) / det;
                    X[kn] = conjf((-c * y1 + a * y2) / det);
                }
            }
            cf acc = 0;
            for (int i = 0; i < n_pilot; i++)
                acc += X[kidx(pilot_k[i])] * pilot_value(pilot_k[i]);
            cf rot = cexpf(-I * cargf(acc));
            for (int i = 0; i < n_data; i++) {
                cf x = X[kidx(data_k[i])] * rot;
                u32 st = xs_state, w = bit_word;
                int bp = bit_pos;
                cf ref = qam_next(m);
                xs_state = st; bit_word = w; bit_pos = bp;       /* replay the same bits */
                errs += axis_errors(crealf(x), m / 2, sc);
                errs += axis_errors(cimagf(x), m / 2, sc);
                cf e = x - ref;
                e2 += crealf(e) * crealf(e) + cimagf(e) * cimagf(e);
                s2 += crealf(ref) * crealf(ref) + cimagf(ref) * cimagf(ref);
            }
        }
        double evm = 10 * log10(e2 / s2);
        if (wl) { r->evm_wl_db = evm; r->err_wl = errs; }
        else    { r->evm_lin_db = evm; r->err_lin = errs; }
    }
    r->bits = N_DATA_SYM * n_data * m;
    return best > 0 ? 0 : -1;
}

void ofdm_print(const ofdm_result *r)
{
    printf("OFDM RX %d-QAM: frame at %d, Q sign %+d lag %+d, image rejection %.1f dB\r\n"
           "  linear EQ        EVM %6.1f dB  bit errors %6d / %d\r\n"
           "  widely linear EQ EVM %6.1f dB  bit errors %6d / %d\r\n",
           1 << cur_m, r->frame_pos, r->q_sign, r->q_lag, r->irr_db,
           r->evm_lin_db, r->err_lin, r->bits, r->evm_wl_db, r->err_wl, r->bits);
}
