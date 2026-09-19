#!/usr/bin/env python3
"""Lane T3-DESIGN: circle through the three anchors of a probe record, its impact parameter, helpers."""
import numpy as np


def circle(xyz):
    """xyz: (n, 6, 3) hit positions; anchors are hits 0, 2, 4. Returns R, cx, cy, d0 = |hypot(cx, cy) - R|."""
    x1, y1 = xyz[:, 0, 0].astype(float), xyz[:, 0, 1].astype(float)
    x2, y2 = xyz[:, 2, 0].astype(float), xyz[:, 2, 1].astype(float)
    x3, y3 = xyz[:, 4, 0].astype(float), xyz[:, 4, 1].astype(float)
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    with np.errstate(divide="ignore", invalid="ignore"):
        cx = ((x1**2 + y1**2) * (y2 - y3) + (x2**2 + y2**2) * (y3 - y1) + (x3**2 + y3**2) * (y1 - y2)) / d
        cy = ((x1**2 + y1**2) * (x3 - x2) + (x2**2 + y2**2) * (x1 - x3) + (x3**2 + y3**2) * (x2 - x1)) / d
    R = np.hypot(x1 - cx, y1 - cy)
    return R, cx, cy, np.abs(np.hypot(cx, cy) - R)
