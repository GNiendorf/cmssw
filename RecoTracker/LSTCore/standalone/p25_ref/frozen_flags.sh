#!/bin/bash
# The M19 FROZEN command, COMPLETE (P2.4 scope: the attach block is live).
#
# Source of truth: standalone/fanout5/final/FREEZE_RECORD.txt section 1, reproduced in the
# P2_PORT_MAP.md FREEZE ADDENDUM. Identical to p23_ref/frozen_flags.sh except that the trailing
# "-a 999" (which made the attach margin unreachable and kept P2.3's attach inert) is GONE, so the
# effective attach margin is the frozen -a 6.875 and -RPS 1 / -RD 1 are live.
FROZEN_FLAGS=(
  -e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9
  -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2
  -C25 2.0 -C25D -2.0
  -A 4 -a 999 -D 5 -RT5 1
  -BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1
  -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1
  -a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0
)
