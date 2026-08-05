#!/bin/bash
# re_run.sh -- RECON C (efficiency headroom) driver. Flags-only: runs the GOLDEN binary
# S/fanout4/compose_attach/bin/chainproto, writes everything into S/fanout5/recon_eff.
#   re_run.sh <tag> [flag overrides...]
# Base command = ANCHOR + CTL + STACK ; overrides appended last so they win.
# Pass STACK=""  in the environment to run the DEFAULTS repro (ANCHOR + CTL only).
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G="$S/fanout4/compose_attach"
P="$S/fanout5/recon_eff"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
if [ -z "${STACK+x}" ]; then
  STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1"
fi
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/re_${TAG}.root" "$P/re_${TAG}_hists.root"
echo "OVERRIDES: $*" > "$P/re_${TAG}.cmd"
echo "STACK: $STACK" >> "$P/re_${TAG}.cmd"
"$G/bin/chainproto" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/re_${TAG}.root" $ANCHOR $CTL $STACK "$@" > "$P/re_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/re_${TAG}.log"; exit 1; }
createPerfNumDenHists -i "$P/re_${TAG}.root" -o "$P/re_${TAG}_hists.root" >> "$P/re_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/re_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/re_${TAG}.json" > "$P/re_agg_${TAG}.txt" 2>/dev/null
echo "[re] DONE $TAG"
