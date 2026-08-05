#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_dup
FL="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1"
$P/rd_run.sh dd2      $FL -DD 2 -DDK 1
$P/rd_run.sh dd2f30   $FL -DD 2 -DDF 0.30 -DDK 1
$P/rd_run.sh dd2f40   $FL -DD 2 -DDF 0.40 -DDK 1
$P/rd_run.sh dd2p1    $FL -DD 2 -DDP 1 -DDK 1
$P/rd_run.sh dd2k0    $FL -DD 2 -DDK 0
touch $P/levers.done
