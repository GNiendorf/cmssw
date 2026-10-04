#!/bin/bash
# chain.sh: progress line every 30 min (PROGRESS.txt) from the scheduler's state lines; when sched.log has ALL DONE / STOPPED -> post.sh
# (final, or partial if STOP exists); at 9:00 am if not done -> STOP (running jobs finish, no new ones), then post.sh partial.
V=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu/val5k
log() { echo "$(date '+%I:%M %p') $*" >> $V/PROGRESS.txt; }
DEADLINE=$(date -d 'tomorrow 09:00' +%s); [ $(date +%H) -lt 9 ] && DEADLINE=$(date -d 'today 09:00' +%s)
last=0
while true; do
  if grep -q "ALL DONE\|STOPPED" $V/sched.log 2>/dev/null; then m=$([ -e $V/STOP ] && echo partial || echo final); log "chain: scheduler finished; post.sh $m"; bash $V/scripts/post.sh $m > $V/logs/post.out 2>&1; log "chain: post.sh exit $?"; break; fi
  if [ $(date +%s) -ge $DEADLINE ] && [ ! -e $V/STOP ]; then touch $V/STOP; log "chain: 9:00 am reached, STOP (running jobs finish, then partial post)"; fi
  if [ $(( $(date +%s) - last )) -ge 1800 ]; then last=$(date +%s)
    st=$(grep "state:" $V/sched.log 2>/dev/null | tail -n 1 | cut -d' ' -f3-)
    log "progress: $st; jobs rc=0 $(grep -c 'rc=0' $V/jobs.status 2>/dev/null), non-zero $(grep -vc 'rc=0' $V/jobs.status 2>/dev/null); lane $(du -sh $V 2>/dev/null | cut -f1); disk free $(df -h /mnt/data1 | tail -n 1 | awk '{print $4}')"; fi
  sleep 60
done
