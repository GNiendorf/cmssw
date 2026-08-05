#!/bin/bash
# Grid B: fine -TA resolution between the mandated 1.0 and 3.0, at flagship -TT 1.2.
# RECON-1 F10 says -TA is the efficient dial (81 milli-hits per milli-eff) but -TA 2.0/3.0
# overspend the 0.55 milli-eff floor headroom. This finds where it lands inside the floors.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim
R=$P/xt_run.sh

for TA in 1.25 1.5 1.75 2.0 2.25 2.5; do
  TAG=$(echo $TA | tr -d '.')
  "$R" b_ta${TAG} -TA $TA > "$P/drv_b_ta${TAG}.out" 2>&1 &
done
wait
touch $P/gridB.done
echo "GRIDB DONE"
