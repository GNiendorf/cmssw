#!/usr/bin/env python3
"""M22 CEIL -- WHERE THE CONDITIONED UNIVERSE'S EXTRA ROWS COME FROM, AND WHAT THEY ARE.

The hypothesis's mechanism is pLS LIBERATION: in the unconditioned universe a pixel seed
can be captured by a REDUNDANT T3 in the per-target argmax / one-pLS-one-owner contention
and then thrown away with it, whereas conditioning removes that T3 before the head scores
and leaves the seed free for a T3 that is not redundant. This script measures whether that
happens and whether the liberated rows are NEW tracks or duplicates.

Rows of the conditioned universe are classified against the unconditioned one, per event,
by pixel-seed row:
  SAME      the same pLS ended on the same T3           (the rule changed nothing)
  MOVED     the same pLS ended on a DIFFERENT T3        (liberated: the mechanism)
  NEWSEED   this pLS won nothing in U0 at all           (liberated from a losing contention)
and each class is broken into fake / duplicate-of-already-delivered / genuinely NEW.
"""
import os
import sys

CE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, CE)
from ce_lab import load  # noqa: E402


def classify(e, o, covFull):
    if o[3]:
        return 'fake'
    if all(m[0] in covFull for m in o[4]):
        return 'dup'
    return 'new'


def main():
    theta = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
    nev = int(sys.argv[2]) if len(sys.argv) > 2 else None
    cond = sys.argv[3] if len(sys.argv) > 3 else 'C1'
    U = load(os.path.join(CE, 'aud_U0.txt'), nev)
    C = load(os.path.join(CE, 'aud_%s.txt' % cond), nev)
    n = min(len(U), len(C))
    print('== mechanism: %s vs U0, theta=%.1f, %d events ==' % (cond, theta, n))
    tot = {}
    for i in range(n):
        eu, ec = U[i], C[i]
        # The pixel-hit tuple identifies the seed uniquely (the same 3-4 ph2/pix rows),
        # so it is the join key between the two universes' delivery winners.
        upix = {}
        for o in eu.O:
            if o[0] >= theta:
                upix[o[8]] = o[11]
        cov = set(ec.alreadyFull)
        for o in sorted([o for o in ec.O if o[0] >= theta], key=lambda o: (-o[0], o[11])):
            key = o[8]
            if key not in upix:
                cls = 'NEWSEED'
            elif upix[key] == o[11]:
                cls = 'SAME'
            else:
                cls = 'MOVED'
            kind = classify(ec, o, cov)
            tot[(cls, kind)] = tot.get((cls, kind), 0) + 1
            for m in o[4]:
                cov.add(m[0])
    print('%-10s %10s %10s %10s %10s' % ('class', 'fake/e', 'dup/e', 'NEW/e', 'rows/e'))
    for cls in ('SAME', 'MOVED', 'NEWSEED'):
        f = tot.get((cls, 'fake'), 0) / n
        d = tot.get((cls, 'dup'), 0) / n
        w = tot.get((cls, 'new'), 0) / n
        print('%-10s %10.1f %10.1f %10.1f %10.1f' % (cls, f, d, w, f + d + w))
    f = sum(v for k, v in tot.items() if k[1] == 'fake') / n
    d = sum(v for k, v in tot.items() if k[1] == 'dup') / n
    w = sum(v for k, v in tot.items() if k[1] == 'new') / n
    print('%-10s %10.1f %10.1f %10.1f %10.1f' % ('TOTAL', f, d, w, f + d + w))


if __name__ == '__main__':
    main()
