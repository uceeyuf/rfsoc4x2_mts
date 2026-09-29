"""Plot ADC captures dumped by sw/dump_captures.tcl and measure the channel delay.

    python host/plot_captures.py out                     # one capture set
    python host/plot_captures.py out_nomts out_mts       # without / with MTS
    python host/plot_captures.py out_nomts out_mts out_align   # + alignment

Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
"""
import os
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

FS = 4.0e9
SURF, INK, INK2, MUTED, GRID, BASE = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9', '#c3c2b7'
C_B, C_D = '#2a78d6', '#eb6834'


def load(d, name):
    return np.fromfile(os.path.join(d, name + '.bin'), dtype='<i2').astype(float)


def delay(x, y, n=8192, lags=512, start=4096):
    """delay of y vs x in samples (positive: y later) and the sign of the peak"""
    r = np.array([np.dot(x[start:start + n], y[start + k:start + k + n]) for k in range(-lags, lags + 1)])
    b = int(np.argmax(np.abs(r)))
    a, c = np.abs(r[b - 1]), np.abs(r[b + 1])
    frac = 0.5 * (a - c) / (a - 2 * np.abs(r[b]) + c) if 0 < b < 2 * lags else 0.0
    return b - lags + frac, r[b] < 0


plt.rcParams.update({'font.family': 'Segoe UI', 'font.size': 10, 'text.color': INK,
                     'axes.edgecolor': BASE, 'axes.labelcolor': INK2, 'xtick.color': MUTED,
                     'ytick.color': MUTED, 'axes.facecolor': SURF, 'figure.facecolor': SURF})

dirs = sys.argv[1:] or ['out']
titles = {2: {0: 'without MTS', 1: 'with MTS'},
          3: {0: 'without MTS', 1: 'with MTS', 2: 'MTS + alignment'}}.get(len(dirs), {})
fig, axes = plt.subplots(len(dirs), 1, figsize=(9, 2.9 * len(dirs)), squeeze=False, sharex=True)
for i, (ax, d) in enumerate(zip(axes[:, 0], dirs)):
    b, dd = load(d, 'adc_b'), load(d, 'adc_d')
    lag, inv = delay(b, dd)
    if inv:
        dd = -dd                      # ADC_D is inverted on the RFSoC4x2
    s, n = 4096, 96
    t = np.arange(n) / FS * 1e9
    ax.plot(t, b[s:s + n] / 32768, color=C_B, lw=2, label='ADC_B (tile 226) <- DAC_A (tile 230)')
    ax.plot(t, dd[s:s + n] / 32768, color=C_D, lw=2, label='ADC_D (tile 224) <- DAC_B (tile 228)')
    ax.set_title('%s: ADC_D vs ADC_B  %+.3f samples (%+.1f ps)' % (titles.get(i, d), lag, lag * 250),
                 loc='left', fontsize=11, color=INK)
    ax.grid(axis='y', color=GRID, lw=0.8)
    ax.set_axisbelow(True)
    ax.tick_params(length=0)
    for k in ('top', 'right'):
        ax.spines[k].set_visible(False)
    ax.set_ylabel('full scale')
    print('%s: delay %+.3f samples = %+.1f ps%s' % (d, lag, lag * 250, ' (inverted)' if inv else ''))
axes[-1, 0].set_xlabel('ns (4.0 GSPS, chirp 100 MHz -> 1.5 GHz)')
handles, labels = axes[0, 0].get_legend_handles_labels()
fig.legend(handles, labels, frameon=False, fontsize=9, loc='upper center', ncol=2)
fig.tight_layout(rect=(0, 0, 1, 0.94))
out = 'mts_captures.png'
fig.savefig(out, dpi=150)
print('wrote', out)
