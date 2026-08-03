#!/bin/bash
# rb_all.sh -- P1 RE-BASELINE measurement set on the frozen 300-event instrumented subset.
# Runs sequentially (each run is ~2 min; no timing claim is made from these).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/rebase_ref"
LSTN="$P/LSTNtuple_instr_300evt.root"
TRK=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/

pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

# 0) CURRENT LST on these events: -m identity re-emits LST's own TC collection verbatim.
#    Its hists become the `base` column every compare_ab.py call is scored against.
if [ ! -f "$P/rb_base300_hists.root" ]; then
  echo "[rb_all] identity (current LST) -> rb_base300"
  "$S/protoBASE/bin/chainproto" -m identity -i "$LSTN" -t "$TRK" -n -1 \
      -o "$P/rb_base300.root" > "$P/rb_base300.log" 2>&1 || { echo IDENTITY FAILED; exit 1; }
  createPerfNumDenHists -i "$P/rb_base300.root" -o "$P/rb_base300_hists.root" >> "$P/rb_base300.log" 2>&1
fi

FULL="-ZPF 3 -ZP5 1 -RT3 1 -T3E 0"

bash "$P/rb_run.sh" RBBASE                        # frozen line, no deletion flags
bash "$P/rb_run.sh" POSTDEL   $FULL -ZP8 5        # MEASURED pass-1 universe
bash "$P/rb_run.sh" POSTDELP2 $FULL -ZP8 6        # MEASURED pass-1 + pass-2 universe
bash "$P/rb_run.sh" MODELF3   $FULL -ZP8 3        # audit family model, same events
bash "$P/rb_run.sh" MODELP2   $FULL -ZP8 4        # audit pass-2 model, same events
bash "$P/rb_run.sh" NOZP8     $FULL               # deletion WITHOUT any bare-seed change

python3 "$P/rb_tab.py" RBBASE NOZP8 MODELF3 MODELP2 POSTDEL POSTDELP2 | tee "$P/rb_scoreboard.txt"
