#!/usr/bin/env python3
"""module_times.py a.json [b.json ...]: per-module time_real / time_thread (ms per job event; 'events' = events the
module ran) of the mkFit chain modules from FastTimerService JSONs (menu Phase2Timing_resources.json)."""
import json, sys, re
pat = re.compile(r'MkFit|hltInitialStepTrackCandidates$|hltInitialStepTracks$|Stock$')
for fn in sys.argv[1:]:
    d = json.load(open(fn)); nev = d['total']['events']
    print('%s (%d events; job total real %.1f ms/ev, thread %.1f ms/ev)' % (fn, nev, d['total']['time_real'] / nev,
                                                                           d['total']['time_thread'] / nev))
    tot = 0.
    for m in d['modules']:
        if pat.search(m['label']) and m['type'] not in ('Job',):
            print('   %-50s %-40s ran %4d  real %7.2f  thread %7.2f ms/job-ev' % (m['label'], m['type'], m['events'],
                  m['time_real'] / nev, m['time_thread'] / nev))
