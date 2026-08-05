# B04 -- FREE EXPLORATION AGENT (barrel duplicate, unassigned angle)

Workspace: `standalone/protoB04` (copy of protoFINAL + one new INERT diagnostic `-B4D`
and one new OFF-BY-DEFAULT mechanism `-XCQ`). Artifacts: `standalone/b04_ref`.

## CHOSEN ANGLE -- "OWNER CREDIBILITY", not seed score

The diagnosis' honest verdict is that the barrel class-A / class-B separation on the
attach PAIR LOGIT is intrinsically weak, and that closing the barrel needs "a mechanism
that separates class A from class B on something OTHER than this logit". The three
assigned angles work the seed side (band thresholds on that logit), the chain+seeded
cell, and the fake branches.

My angle is the OTHER side of the same pair: WHO the owner is.
`-XCT` retires a bare seed when SOME delivered seedless chain within the dR window scores
the pair above a bar. That bar is flat over targets, but the targets are wildly unequal:
by `tc_dbgBr` the barrel seedless chains are br2 (5+ layer, IP-compatible) 5.3% fake,
br3 (5+ layer, displaced-exempt) 26.0% fake, br1 (4-layer exempt) 67.4% fake. A seed
that pairs with a br2 chain is far more likely to actually BE that chain's twin than a
seed that pairs with a br1 chain; and when the "owner" is itself a fake, retiring the
seed is pure efficiency loss. So: let a credible owner retire a seed at a lower bar than
an incredible one. Existing quantities only (window, pair log, -G 6 branch code), no new
proximity/embedding test, no pairwise candidate loop, one extra constant.

## STATE
* M0 workspace built. `protoB04/bin/chainproto` md5 f5c382ee21c6c62c4a163a53024fcf16
  (differs from protoFINAL's 45c68733... only through the embedded build path; the gate
  is the output comparison, not the md5).
* M1 `-B4D` instrument written: band x cover-kind x fate table of the post-deletion bare
  seed universe, plus one `B4P` row per (takeable seed, in-window delivered seedless
  chain) pair carrying the pair logit AND the owner's branch / 3-class margin / layer
  count / weld score.
* M1 `-XCQ <bar>` mechanism written, OFF by default (1e9 sentinel), with `-XCQE` (band
  edge, default 1.1 = barrel only), `-XCQB` (which branch counts as strong, default 2)
  and `-XCQM` (extra margin requirement on the owner).
* GATE RUN in flight: tag `B4G0` = CHAINFINAL line + `-B4D 2`, 300 events.
