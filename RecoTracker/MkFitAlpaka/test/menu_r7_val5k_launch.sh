#!/bin/bash
# launch.sh: pilot (10 events per arm on the first local file, outside runs/) then the scheduler + chain (setsid; PIDs -> pids.txt).
# Preconditions: setup_area.sh done; disk >= 50 GB free; <= 2 other cmsRun jobs on the box. Controls: maxj (default 2; 4 = the box cap if
# nothing else runs), nfetch, stage_gb, 'stream' (xrootd streaming, ON), STOP (drain), GO + <arm>.READY (written here after the pilot).
V=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r7_menu/val5k
log() { echo "$(date '+%I:%M %p') $*" >> $V/PROGRESS.txt; }
[ $(df --output=avail -BG /mnt/data1 | tail -1 | tr -dc 0-9) -ge 50 ] || { echo "disk < 50 GB free"; exit 1; }
F=$(awk 'NF>1{print $2; exit}' /mnt/data1/gsn27/here/hltqcd/val5k/files_ttbar.txt)
mkdir -p $V/pilot; for a in ref target tgpu; do ( sed "s|D=\$L/runs/\$A/\$SMP/\$T|D=\$L/pilot/\$A|" $V/scripts/job.sh > $V/pilot/job_$a.sh; bash $V/pilot/job_$a.sh $a ttbar $F pilot 10 > $V/pilot/$a.out 2>&1 ) & done; wait
for a in ref target tgpu; do grep -q "^OK" $V/pilot/$a.out && touch $V/$a.READY; log "pilot $a: $(tail -n 1 $V/pilot/$a.out)"; done
ls $V/*.READY > /dev/null 2>&1 || { echo "no arm passed the pilot"; exit 1; }
touch $V/GO
setsid nohup python3 $V/scripts/scheduler.py > $V/logs/sched.out 2>&1 < /dev/null & echo "$! scheduler (setsid) $(date '+%I:%M %p')" >> $V/pids.txt
setsid nohup bash $V/scripts/chain.sh > $V/logs/chain.out 2>&1 < /dev/null & echo "$! chain (setsid) $(date '+%I:%M %p')" >> $V/pids.txt
log "val5k-r7 launched: arms $(ls $V/*.READY | xargs -n1 basename | tr '\n' ' ')"
