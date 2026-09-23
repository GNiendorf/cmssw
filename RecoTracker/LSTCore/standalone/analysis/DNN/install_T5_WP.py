#!/usr/bin/env python3
"""Write the T5 DNN creation WP table (kWp, on 1 - P(fake)) from a wp_T5_DNN.py json into interface/alpaka/Common.h
(between the WP_TABLES_BEGIN/END markers of namespace t5dnn), and optionally the exported weights header.
Every created T5 is TC-eligible: a separate promotion table was measured to change nothing (deploy scan 2026-09-22)."""
import argparse
import json
import shutil

ap = argparse.ArgumentParser()
ap.add_argument("--json", required=True)
ap.add_argument("--creation", required=True, help="retention key, e.g. 0.95")
ap.add_argument("--common-h", required=True)
ap.add_argument("--weights-src")
ap.add_argument("--weights-dst")
a = ap.parse_args()
c = json.load(open(a.json))["tables"][str(float(a.creation))]["wp"]
rows = ",\n          ".join("{" + ", ".join(f"{x:.4f}f" for x in r) + "}" for r in c)
blk = f"""      // WP_TABLES_BEGIN
      // Creation: 1 - P(fake) keeps {a.creation} of fully matched T5s per bin (first anchor |eta|).
      HOST_DEVICE_CONSTANT float kWp[kPtBins][kEtaBins] = {{
          {rows}}};
      // WP_TABLES_END"""
s = open(a.common_h).read()
i, j = s.index("      // WP_TABLES_BEGIN"), s.index("      // WP_TABLES_END") + len("      // WP_TABLES_END")
open(a.common_h, "w").write(s[:i] + blk + s[j:])
if a.weights_src:
    shutil.copyfile(a.weights_src, a.weights_dst)
print("installed creation", a.creation)
