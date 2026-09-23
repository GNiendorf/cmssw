#!/usr/bin/env python3
"""Plot GPU memory time series from gpu_mem_profile.sh: one panel per sample, one curve per arm, peak in the legend.
usage: gpu_mem_plot.py <out.png> <ctx1,ctx2,...> <arm1,arm2,...>   (reads deploy/gpumem/<arm>_<ctx>.csv)"""
import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "deploy/gpumem")
out, ctxs, arms = sys.argv[1], sys.argv[2].split(","), sys.argv[3].split(",")
fig, axes = plt.subplots(1, len(ctxs), figsize=(6 * len(ctxs), 4.2), squeeze=False)
for ax, ctx in zip(axes[0], ctxs):
    for arm in arms:
        fn = f"{D}/{arm}_{ctx}.csv"
        if not os.path.exists(fn):
            continue
        rows = list(csv.DictReader(open(fn)))
        t = [float(r["t_s"]) for r in rows]
        m = [float(r["used_MiB"]) for r in rows]
        ax.plot(t, m, label=f"{arm}  (peak {max(m):.0f} MiB)", lw=1.6)
    ax.set_title(f"{ctx}: GPU memory used by lst_cuda (100 events, 4 streams)")
    ax.set_xlabel("time since start [s]")
    ax.set_ylabel("device memory [MiB]")
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
fig.tight_layout()
fig.savefig(out, dpi=120)
print("wrote", out)
