#!/usr/bin/env python3
"""Quick matplotlib render of the exported shells (no viewer needed).
   Usage: render_check.py out.png  → 4 panels of front/back shells."""
import sys, struct, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

HERE = os.path.dirname(__file__)
OUT = os.path.join(HERE, "out")


def load_stl(path):
    with open(path, "rb") as f:
        f.read(80)
        n = struct.unpack("<I", f.read(4))[0]
        tris = np.zeros((n, 3, 3), dtype=np.float32)
        for i in range(n):
            f.read(12)                                   # normal
            tris[i] = struct.unpack("<9f", f.read(36))[0:9] \
                if False else np.array(struct.unpack("<9f", f.read(36))).reshape(3, 3)
            f.read(2)
    return tris


def panel(ax, tris, title, elev, azim, color):
    ax.add_collection3d(Poly3DCollection(tris, facecolor=color, edgecolor="k",
                                         linewidths=0.05, alpha=1.0))
    pts = tris.reshape(-1, 3)
    lo, hi = pts.min(0), pts.max(0)
    c = (lo + hi) / 2
    r = (hi - lo).max() / 2
    ax.set_xlim(c[0] - r, c[0] + r); ax.set_ylim(c[1] - r, c[1] + r)
    ax.set_zlim(c[2] - r, c[2] + r)
    ax.set_box_aspect((1, 1, 1)); ax.view_init(elev=elev, azim=azim)
    ax.set_title(title, fontsize=9); ax.set_axis_off()


def main(outpng):
    back = load_stl(f"{OUT}/back_shell.stl")
    front = load_stl(f"{OUT}/front_shell.stl")
    fig = plt.figure(figsize=(13, 10))
    panel(fig.add_subplot(221, projection="3d"), back,
          "BACK tub — interior (pegs + clips + lip)", 35, -90, "#3a3a3a")
    panel(fig.add_subplot(222, projection="3d"), back,
          "BACK tub — top rim (lip)", 12, -90, "#3a3a3a")
    panel(fig.add_subplot(223, projection="3d"), front,
          "FRONT lid — underside (groove + board opening)", -35, -90, "#c8a200")
    panel(fig.add_subplot(224, projection="3d"), front,
          "FRONT lid — outside", 30, -90, "#c8a200")
    fig.tight_layout()
    fig.savefig(outpng, dpi=110)
    print("wrote", outpng)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else f"{OUT}/v9_check.png")
