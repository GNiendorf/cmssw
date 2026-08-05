#!/bin/bash
# Regenerate the whole synthesis scoreboard.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
cd "$S"
T="python3 $S/synth_ref/syn_tab.py"
echo "=== A. HEADLINE, FROZEN 300 ==="; $T FINBASE C1 C2 C3 D1 E4 E1 D4 D3
echo; echo "=== B. PER REGION, FROZEN 300 ==="; $T -r FINBASE D1 E4
echo; echo "=== C. DISPLACED BANDS, FROZEN 300 ==="; $T -b FINBASE D1 E4
echo; echo "=== D. HEADLINE, FULL 977 (NUMBERS OF RECORD) ==="; $T W_X4 W_D1 W_E4 W_C2_A60R50 W_XC4
echo; echo "=== E. PER REGION, FULL 977 ==="; $T -r W_X4 W_D1 W_E4
echo; echo "=== F. DISPLACED BANDS, FULL 977 ==="; $T -b W_X4 W_D1 W_E4
