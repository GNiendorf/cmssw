#!/usr/bin/env python3
"""Per-module mean times from FastTimerService JSON files written by replay_cfg.py timing=1 (harness lane).
   python3 test/replay_timing_summary.py t1.json [t2.json ...]   -> one column per file, ms/event (real time)"""
import json, sys
cols = [json.load(open(f)) for f in sys.argv[1:]]
labels = []
for d in cols:
    for m in d['modules']:
        if m['label'] not in labels:
            labels.append(m['label'])
print('%-42s' % 'module' + ''.join('%14s' % f.split('/')[-1][:13] for f in sys.argv[1:]))
for lab in labels:
    row = '%-42s' % lab
    for d in cols:
        m = [x for x in d['modules'] if x['label'] == lab]
        row += '%14.2f' % (m[0]['time_real'] / max(1, m[0]['events'])) if m else '%14s' % '-'
    print(row)
print('%-42s' % 'events' + ''.join('%14d' % d['total']['events'] for d in cols))
