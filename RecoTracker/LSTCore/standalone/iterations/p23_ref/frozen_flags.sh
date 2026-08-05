#!/bin/bash
# The M19 FROZEN command MINUS the attach block (P2.3 scope).
#
# Source of truth: standalone/fanout5/final/FREEZE_RECORD.txt section 1.
#   ANCHOR   = -e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9
#              -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2
#              -C25 2.0 -C25D -2.0
#   CTL      = -A 4 -a 999 -D 5 -RT5 1
#   FLAGSHIP = -BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1
#              -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1
#   M19      = -a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0
#
# P2.3 deviation, and the ONLY one: a trailing "-a 999" so the attach margin is
# unreachable and no (pLS, chain) pair is ever granted.  Attach is therefore INERT:
#   * no chain is upgraded to type 7,
#   * ga.plsOwned stays all-zero so -RPS suppresses nothing,
#   * ga.chainPls stays all -1 so -RD revokes nothing.
# -RT5 1 still fires (it is wholesale and attach-independent): every carried type-7 row
# is dropped, so the pT5 class is REPLACED by bare chain TCs.  Carried pT3 (type 5) and
# bare pLS (type 8) rows are kept verbatim, and only the pT3 rows pre-claim under -PU 1.
FROZEN_FLAGS=(
  -e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9
  -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2
  -C25 2.0 -C25D -2.0
  -A 4 -a 999 -D 5 -RT5 1
  -BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1
  -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1
  -a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0
  -a 999
)
