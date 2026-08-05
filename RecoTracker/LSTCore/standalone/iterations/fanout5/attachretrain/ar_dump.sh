#!/bin/bash
# ar_dump.sh -- M19 attach-head retrain pair-dump factory.
#
# Regenerates the GENERAL (chain + bare-T3) attach pair dump at the M19 STACK operating
# point over BOTH event sources of the M8 combination:
#   primary : LSTNtuple_PU200RelVal_300evt.root  (300 events)  -> pr_c*.root
#   salvage : LSTNtuple_PU200RelVal_1000evt.root (498 events)  -> sv_c*.root
#
# WHY REGENERATE (and not reuse fanout3/m16/pairs_gen_c*.root): the resident dump was
# produced at the pre-M19 ANCHOR (-F 0.3 -PU 2 -C25 2.0 -M4 3.5 -M4D -0.75 -MRI 0.5).
# The M19 STACK moves every one of those (-F 0.20 -PU 1 -C25 0.0 -M4 4.0 -M4D -1.2
# -MRI -0.5), and the K9 ACCEPTED SET is exactly what defines BOTH target universes
# (chains directly, bare T3s by complement). Measured on evt 0: accepted 130 -> 128,
# bareT3 15866 -> 15873. The resident head therefore trains on a population the FLAGSHIP
# pipeline no longer enumerates.
#
# INERT-IN-PAIRDUMP FLAGS (documented, passed anyway so the command is one string):
#   -BK/-BT (order-key reshape), -TR/-TT/-TA (terminal trim), -ZM4/-ZM4D (transition
#   band gate deltas) are read ONLY by the hybrid-mode block in main.cc; pairdump does
#   not apply them. -F/-G/-M4/-M4D/-MRI/-PU/-C25/-B/-H/-W/-FC ARE honoured (verified:
#   the mode banner prints maxClaimedFrac=0.200 under the STACK).
#   The CTL flags (-A/-a/-D/-RT5) are delivery-side and REJECTED by pairdump by design.
#
# -PDS 64: bare-T3 FAKE write stride (the m16 dump used 32). Fakes carry wgt = stride so
# every weighted loss/AUC term is an unbiased estimator of the FULL undownsampled
# deployed population -- doubling the stride halves the row count and leaves the
# estimator unbiased. True pairs are NEVER downsampled (-PDC 1 for chain targets: the
# chain universe is small enough to keep whole).

# NOTE: no `set -u` -- sourcing setup.sh trips it (unbound vars in the CMSSW env script).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/attachretrain"
D="$P/dump"
mkdir -p "$D"

ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
PD="-PDT 2 -PDC 1 -PDS 64"
TRK=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/

run_chunk() {
  # $1 = prefix (pr|sv)  $2 = input root  $3 = first entry  $4 = n events
  local pre="$1" inp="$2" first="$3" n="$4"
  local tag="${pre}_c$(printf '%02d' $((first / 20)))"
  "$P/bin/chainproto" -m pairdump -i "$inp" -t "$TRK" \
    -o "$D/${tag}.root" -n "$n" -PDN "$first" $ANCHOR $STACK $PD \
    > "$D/${tag}.log" 2>&1
  echo "$tag exit=$? rows=$(grep -c '^evt ' "$D/${tag}.log")"
}
export -f run_chunk
export P D ANCHOR STACK PD TRK

pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

CHUNK=20
JOBS="${1:-20}"
{
  for ((f = 0; f < 300; f += CHUNK)); do
    n=$((300 - f)); [ $n -gt $CHUNK ] && n=$CHUNK
    echo "pr $S/LSTNtuple_PU200RelVal_300evt.root $f $n"
  done
  for ((f = 0; f < 498; f += CHUNK)); do
    n=$((498 - f)); [ $n -gt $CHUNK ] && n=$CHUNK
    echo "sv $S/LSTNtuple_PU200RelVal_1000evt.root $f $n"
  done
} | xargs -P "$JOBS" -L 1 bash -c 'run_chunk $0 $1 $2 $3'

echo "=== DUMP COMPLETE ==="
ls -la "$D"/*.root | wc -l
du -sh "$D"
