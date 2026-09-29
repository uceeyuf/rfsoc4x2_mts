"""Reference model of the I/Q OFDM link (same frame, generators and receiver as sw/src/ofdm.c).

    python host/ofdm.py sim                    # simulated channel: separate I / Q paths
    python host/ofdm.py rx out_ofdm [bits]     # demodulate dumped captures, write ofdm_const.png
    python host/ofdm.py fig out_mts out_align [bits]   # MTS only vs MTS + alignment figure

DAC_A plays I, DAC_B plays Q (baseband for an external I/Q modulator). In the board loopback
ADC_B receives I and ADC_D receives Q (inverted on the RFSoC4x2). I and Q take separate
converter paths, so the receiver uses a widely linear equalizer: per sub-carrier pair
(k, -k) it estimates Y(k) = A(k) X(k) + B(k) X*(-k) from two training symbols.

Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
"""
import os
import sys
import numpy as np

FS = 4.0e9
N, CP = 1024, 128
SYM = N + CP
K_LO, K_HI = 8, 250                 # active sub-carriers +/-8 .. +/-250 (31 .. 977 MHz): the baluns roll off below
PILOT_STEP = 16                     # pilots on k = +/-16, 32, ...
N_DATA_SYM = 26
FRAME = 32768                       # 2 training + 26 data symbols (32256) + 512 zeros
NSAMP = 65536                       # DAC buffer: two identical frames
RMS = 0.25 * 32767                  # per rail, DAC full scale 32767
BACKOFF = 8                         # FFT window starts this far into the cyclic prefix
SMOOTH = 3                          # channel estimates averaged over +/-SMOOTH sub-carriers

ACTIVE = np.array([k for k in range(-K_HI, K_HI + 1) if abs(k) >= K_LO])
PILOTS = np.array([k for k in ACTIVE if k % PILOT_STEP == 0])
DATA = np.array([k for k in ACTIVE if k % PILOT_STEP != 0])
POS = np.arange(K_LO, K_HI + 1)     # k > 0 of each (k, -k) pair


class XorShift32:
    """xorshift32, the same sequence as the C code"""
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self):
        s = self.s
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        self.s = s
        return s


def bits_stream(seed, n):
    g = XorShift32(seed)
    out = np.empty(n, dtype=np.uint8)
    w = 0
    for i in range(n):
        if i % 32 == 0:
            w = g.next()
        out[i] = (w >> (i % 32)) & 1
    return out


def qam_map(b, m):
    """m bits per symbol (2, 4, 6, 8), Gray per axis, unit average power"""
    h = m // 2
    L = 1 << h

    def axis(bb):
        v = 0
        for x in bb:                    # binary from Gray
            v = (v << 1) | (int(x) ^ (v & 1))
        return 2 * v - (L - 1)
    b = b.reshape(-1, m)
    i = np.array([axis(r[:h]) for r in b], dtype=float)
    q = np.array([axis(r[h:]) for r in b], dtype=float)
    scale = np.sqrt(2 * (L * L - 1) / 3)
    return (i + 1j * q) / scale


def qam_slice_bits(x, m):
    h = m // 2
    L = 1 << h
    scale = np.sqrt(2 * (L * L - 1) / 3)

    def axis(v):
        idx = np.clip(np.round((v * scale + (L - 1)) / 2), 0, L - 1).astype(int)
        g = idx ^ (idx >> 1)            # Gray
        return [(g >> (h - 1 - j)) & 1 for j in range(h)]
    out = []
    for s in x:
        out += axis(s.real) + axis(s.imag)
    return np.array(out, dtype=np.uint8)


def training():
    """QPSK on every active sub-carrier; symbol 2 negates k < 0"""
    g = XorShift32(0x1234ABCD)
    t1 = np.zeros(N, complex)
    for k in ACTIVE:
        r = g.next()
        t1[k % N] = ((1 if r & 1 else -1) + 1j * (1 if r & 2 else -1)) / np.sqrt(2)
    t2 = t1.copy()
    for k in ACTIVE[ACTIVE < 0]:
        t2[k % N] = -t1[k % N]
    return t1, t2


def pilot_value(k):
    return 1.0 if (k // PILOT_STEP) % 2 == 0 else -1.0


def bits_per_frame(m):
    return N_DATA_SYM * len(DATA) * m


def tx_frame(m):
    """complex baseband frame (FRAME samples, unit per-rail RMS) and the payload bits"""
    t1, t2 = training()
    bits = bits_stream(0xC0FFEE00 + m, bits_per_frame(m))
    syms = qam_map(bits, m).reshape(N_DATA_SYM, len(DATA))
    freq = [t1, t2]
    for s in range(N_DATA_SYM):
        f = np.zeros(N, complex)
        f[DATA % N] = syms[s]
        f[PILOTS % N] = [pilot_value(k) for k in PILOTS]
        freq.append(f)
    x = np.zeros(FRAME, complex)
    for s, f in enumerate(freq):
        t = np.fft.ifft(f) * N
        x[s * SYM:(s + 1) * SYM] = np.concatenate([t[-CP:], t])
    x /= np.sqrt(np.mean(x[:len(freq) * SYM].real ** 2))
    return x, bits, syms


def tx_rails(m):
    """int16 I (DAC_A) and Q (DAC_B), NSAMP samples: two identical frames"""
    x, bits, syms = tx_frame(m)
    x = np.tile(x, NSAMP // FRAME) * RMS
    i = np.clip(np.round(x.real), -32767, 32767).astype(np.int16)
    q = np.clip(np.round(x.imag), -32767, 32767).astype(np.int16)
    return i, q, bits, syms


def rx(adc_i, adc_q, m):
    """demodulate one frame from the two ADC captures; returns a dict of results"""
    t1, t2 = training()
    ref = (np.fft.ifft(t1) * N)                 # training body, time domain
    ai = adc_i.astype(float)
    aq = adc_q.astype(float)

    # frame timing on the I rail against Re{training}
    rr = ref.real
    c = np.correlate(ai[:FRAME + N], rr, mode='valid')[:FRAME]
    p = int(np.argmax(np.abs(c)))               # start of the training body
    # Q rail: polarity (and its offset, for information) against Im{training}
    lags = np.arange(-CP, CP + 1)
    cq = np.array([np.dot(aq[p + l:p + l + N], ref.imag) if 0 <= p + l and p + l + N <= len(aq) else 0
                   for l in lags])
    lq = int(np.argmax(np.abs(cq)))
    q_sign = -1.0 if cq[lq] < 0 else 1.0
    q_lag = int(lags[lq])
    r = ai + 1j * q_sign * aq

    start = p - CP - BACKOFF                    # first symbol incl. CP, window backed off
    if start < 0:
        start += FRAME
    Y = []
    for s in range(2 + N_DATA_SYM):
        o = start + s * SYM + CP
        Y.append(np.fft.fft(r[o:o + N]) / N)
    Y = np.array(Y)

    idx = lambda k: np.asarray(k) % N
    Y1, Y2 = Y[0], Y[1]
    # linear estimate
    H = np.zeros(N, complex)
    H[idx(ACTIVE)] = 0.5 * (Y1[idx(ACTIVE)] / t1[idx(ACTIVE)] + Y2[idx(ACTIVE)] / t2[idx(ACTIVE)])
    # widely linear estimate: A(k), B(k)
    A = np.zeros(N, complex)
    B = np.zeros(N, complex)
    kp, kn = idx(POS), idx(-POS)
    A[kp] = (Y1[kp] + Y2[kp]) / (2 * t1[kp])
    B[kp] = (Y1[kp] - Y2[kp]) / (2 * np.conj(t1[kn]))
    A[kn] = (Y1[kn] - Y2[kn]) / (2 * t1[kn])
    B[kn] = (Y1[kn] + Y2[kn]) / (2 * np.conj(t1[kp]))
    # the channel is smooth over 3.9 MHz: average the estimates over neighbouring sub-carriers
    # (each side of DC, after removing the phase slope of the window back-off). Only with I and
    # Q aligned: a large I/Q offset makes A and B rotate quickly from one sub-carrier to the next.
    if abs(q_lag) <= 2:
        for arr in (H, A, B):
            for sgn in (1, -1):
                ks = sgn * POS
                ph = np.exp(-2j * np.pi * ks * BACKOFF / N)
                v = arr[ks % N] / ph
                arr[ks % N] = np.array([v[max(0, i - SMOOTH):i + SMOOTH + 1].mean() for i in range(len(v))]) * ph
    irr = 10 * np.log10(np.abs(A[idx(ACTIVE)]) ** 2 / np.maximum(np.abs(B[idx(ACTIVE)]) ** 2, 1e-30))

    def equalize(Yd, wl):
        X = np.zeros(N, complex)
        if not wl:
            X[idx(ACTIVE)] = Yd[idx(ACTIVE)] / H[idx(ACTIVE)]
        else:
            # [Y(k); Y*(-k)] = [[A(k), B(k)], [B*(-k), A*(-k)]] [X(k); X*(-k)]
            a, b = A[kp], B[kp]
            c_, d = np.conj(B[kn]), np.conj(A[kn])
            y1, y2 = Yd[kp], np.conj(Yd[kn])
            det = a * d - b * c_
            X[kp] = (d * y1 - b * y2) / det
            X[kn] = np.conj((-c_ * y1 + a * y2) / det)
        # common phase from the pilots
        pv = np.array([pilot_value(k) for k in PILOTS])
        ph = np.angle(np.sum(X[idx(PILOTS)] * pv))
        return X * np.exp(-1j * ph)

    _, bits, syms = tx_frame(m)
    out = {'p': p, 'q_sign': q_sign, 'q_lag': q_lag, 'irr_db': float(np.median(irr))}
    for wl in (False, True):
        Xd = np.array([equalize(Y[2 + s], wl)[idx(DATA)] for s in range(N_DATA_SYM)])
        err = Xd - syms
        evm = np.sqrt(np.mean(np.abs(err) ** 2) / np.mean(np.abs(syms) ** 2))
        rb = qam_slice_bits(Xd.ravel(), m)
        nerr = int(np.sum(rb != bits))
        key = 'wl' if wl else 'lin'
        out[key] = {'evm_db': 20 * np.log10(evm), 'evm_pct': 100 * evm, 'bit_errors': nerr, 'X': Xd}
    out['bits'] = len(bits)
    return out


def rate_gbps(m):
    return bits_per_frame(m) / (FRAME / FS) / 1e9


def simulate(m=4, snr_db=35.0, d_i=0.0, d_q=0.0, g_q=1.02, q_inv=True, seed=1):
    """two real paths with their own gain, fractional delay and a mild low-pass"""
    rng = np.random.default_rng(seed)
    i, q, _, _ = tx_rails(m)

    def path(x, delay, gain, fc):
        f = np.fft.rfftfreq(len(x), 1 / FS)
        Hf = gain * np.exp(-2j * np.pi * f * delay / FS) / (1 + 1j * f / fc)
        y = np.fft.irfft(np.fft.rfft(x.astype(float)) * Hf, len(x))
        return y
    yi = path(i, 37.3 + d_i, 0.43, 2.0e9)
    yq = path(q, 37.3 + d_q, 0.43 * g_q, 1.8e9) * (-1 if q_inv else 1)
    shift = 12345
    yi, yq = np.roll(yi, -shift), np.roll(yq, -shift)
    nstd = np.std(yi) / 10 ** (snr_db / 20)
    yi += rng.normal(0, nstd, len(yi))
    yq += rng.normal(0, nstd, len(yq))
    return np.round(yi).astype(np.int16), np.round(yq).astype(np.int16)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'fig':
        figure(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 8, 'ofdm_const.png')
        return
    if len(sys.argv) > 1 and sys.argv[1] == 'rx':
        d = sys.argv[2]
        m = int(sys.argv[3]) if len(sys.argv) > 3 else 4
        ai = np.fromfile(os.path.join(d, 'adc_b.bin'), dtype='<i2')
        aq = np.fromfile(os.path.join(d, 'adc_d.bin'), dtype='<i2')
        res = rx(ai, aq, m)
        report(res, m, d)
        plot(res, m, 'ofdm_const.png')
        return
    for m in (2, 4, 6, 8):
        for dq in (0.0, 1.0, 60.0):
            ai, aq = simulate(m=m, d_q=dq)
            res = rx(ai, aq, m)
            report(res, m, 'sim Q delay %+.1f' % dq)


def report(res, m, tag):
    print('%-18s %2d bit: frame at %5d, Q sign %+d lag %+4d, IRR %5.1f dB | linear EQ %6.1f dB %6d err | '
          'WL EQ %6.1f dB %6d err / %d bits, %.2f Gb/s'
          % (tag, m, res['p'], res['q_sign'], res['q_lag'], res['irr_db'],
             res['lin']['evm_db'], res['lin']['bit_errors'], res['wl']['evm_db'], res['wl']['bit_errors'],
             res['bits'], rate_gbps(m)))


def figure(d_mts, d_align, m, out):
    """three constellations: MTS only / MTS + align with the linear EQ, MTS + align widely linear"""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    SURF, INK, MUTED, GRID, BASE, C = '#fcfcfb', '#0b0b0b', '#898781', '#e1e0d9', '#c3c2b7', '#2a78d6'
    plt.rcParams.update({'font.family': 'Segoe UI', 'font.size': 10, 'text.color': INK, 'axes.edgecolor': BASE,
                         'xtick.color': MUTED, 'ytick.color': MUTED, 'axes.facecolor': SURF, 'figure.facecolor': SURF})
    load = lambda d: [np.fromfile(os.path.join(d, n + '.bin'), dtype='<i2') for n in ('adc_b', 'adc_d')]
    r_mts, r_al = rx(*load(d_mts), m), rx(*load(d_align), m)
    panels = [(r_mts, 'lin', 'MTS only (Q lag %+d)\nlinear equalizer' % r_mts['q_lag']),
              (r_al, 'lin', 'MTS + alignment (Q lag %+d)\nlinear equalizer' % r_al['q_lag']),
              (r_al, 'wl', 'MTS + alignment\nwidely linear equalizer')]
    fig, axes = plt.subplots(1, 3, figsize=(11.5, 4.3))
    for ax, (r, key, title) in zip(axes, panels):
        X = r[key]['X'].ravel()
        ax.plot(X.real, X.imag, '.', ms=0.8, alpha=0.35, color=C, rasterized=True)
        ax.set_title('%s\nEVM %.1f dB, %d / %d bit errors' % (title, r[key]['evm_db'], r[key]['bit_errors'], r['bits']),
                     fontsize=9.5)
        ax.set_aspect('equal')
        ax.set_xlim(-1.5, 1.5)
        ax.set_ylim(-1.5, 1.5)
        ax.grid(color=GRID, lw=0.6)
        ax.set_axisbelow(True)
        ax.tick_params(length=0)
    fig.suptitle('%d-QAM OFDM, I on DAC_A and Q on DAC_B, 4 GSPS loopback (%.2f Gb/s)' % (1 << m, rate_gbps(m)),
                 fontsize=11)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    print('wrote', out)


def plot(res, m, out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 2, figsize=(9, 4.6))
    for ax, key, title in zip(axes, ('lin', 'wl'), ('linear equalizer', 'widely linear equalizer')):
        X = res[key]['X'].ravel()
        ax.plot(X.real, X.imag, '.', ms=1.2, alpha=0.5, color='#2a78d6')
        ax.set_title('%s: EVM %.1f dB' % (title, res[key]['evm_db']), fontsize=10)
        ax.set_aspect('equal')
        ax.set_xlim(-1.6, 1.6)
        ax.set_ylim(-1.6, 1.6)
        ax.grid(color='#e1e0d9', lw=0.6)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    print('wrote', out)


if __name__ == '__main__':
    main()
