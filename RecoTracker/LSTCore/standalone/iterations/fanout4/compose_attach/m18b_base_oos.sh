#!/bin/bash
# M18b Task C step 2: LST BASELINE hists over the same 349 out-of-sample events.
# The baseline is the input ntuple's OWN tc_* block, i.e. exactly what base300_hists.root
# is for the 300-event tuning file -- same tool, same defaults, different event subset.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P=$S/fanout4/compose_attach
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
root -l -b -q "$P/m18b_filter_evts.C(\"$S/LSTNtuple_PU200RelVal_1000evt.root\",\"$P/LSTNtuple_oos349.root\",\"$P/m18b_oos349_evts.txt\")" 2>&1
createPerfNumDenHists -i "$P/LSTNtuple_oos349.root" -o "$P/base_oos349_hists.root" 2>&1 | tail -5
echo BASE_OOS_DONE
