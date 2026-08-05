#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/t3attach_ref"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1; cmsenv > /dev/null 2>&1; source setup.sh > /dev/null 2>&1
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
FL="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1"
M19="-a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0"
run () { tag=$1; shift; "$P/../prototype/bin/chainproto" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ -n 3 \
  -o "$P/m_${tag}.root" $ANCHOR $CTL $FL $M19 -RT3 1 -AT3 6 "$@" > "$P/m_${tag}.log" 2>&1
  echo "== $tag rc=$? =="; grep -E 'map candidates|M20 candfind|M20 candmap|M20 pT3 dedup|M16 delivery' "$P/m_${tag}.log"; }
run txt  -CF 2 -CFM "$P/cands_test.txt"
run bin  -CF 2 -CFM "$P/cands_test.bin"
run txtW -CF 2 -CFM "$P/cands_test.txt" -CFW 1
run bad  -CF 2 -CFM "$P/does_not_exist.txt"
echo MAPDONE
