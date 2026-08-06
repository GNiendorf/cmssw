# R1 - THE ATTACH HEAD (features, labels, training distribution)

READ FIRST: standalone/FINDINGS_R2.md - the baseline, the inherited facts (do not re-measure
them), the traps, the tooling, and the hard architecture constraint. It also lists the other four
agents' assigned directions so you know what is not yours.

## YOUR LAYER

The attach head itself - the ONE network that decides whether a pLS and an OT object are the same
track. Everything about it is yours: its input features, its labels, its training distribution,
its architecture/width, its calibration across eta and pT. The scoring code (ChainAttach.h
feature construction + AttachInference), the training data generation, and the weight header.

You are the flagship of this round. The maintainer's stated end state is exactly your layer:
ONE head plus one cut, and everything else deleted. So a better head is not just a dup win - it
is the thing that lets other machinery be removed.

## OUT OF BOUNDS

Deletion / retirement rules (R2), the fake-rate mechanisms (R3), the seed-admission universe
and the -RD dedup family (R4), chain construction (R5). Retraining the head that scores the
bare-T3 stage-B population IS yours - the head is one network and you own all of it. Do not add
a SECOND network.

## THE LEAD (verified, and the maintainer's own suggestion)

Our attach head has NO |eta| input at all, no pT, and none of the features LST spent years
engineering for pT3/pT5 matching. In LST master these exist and are known-good:

  rPhiChiSquared, rzChiSquared, rPhiChiSquaredInwards   (the pixel-to-OT helix fit residuals)
  pixelRadius vs tripletRadius / quintupletRadius, with pixelRadiusError  (the radius PULL)
  |pixelEta|, pixelPt

Find them in the master tree (RecoTracker/LSTCore, PixelTriplet.h / the pT3 DNN inputs), read
what they actually measure, and decide which are cheap enough to compute in our attach scorer.
The radius pull is the one to look at first: it is a normalized, dimensionless compatibility
test between the seed's curvature and the OT object's, which is precisely the thing our head is
being asked to judge and currently cannot see.

Time-box the LST archaeology. The maintainer explicitly does not want the round spent reading
LST - get the feature definitions, then work in our tree.

## THE SECOND LEAD (the label, and it may matter more than the features)

D2 measured that among barrel bare-seed survivors, log10(pLS pt) alone has AUC .745 for
"is this a sole cover" while the incumbent pair logit has .598. That is a statement about the
head's TRAINING DISTRIBUTION, not just its features: the head was trained on object-level
sim-matching, and the decisions it is being used for are TC-level. The known analogue is
documented in memory (project_pls_ot_dup_findings): the LST embedding DNN had the same defect.

Also: the maintainer's WP-table practice from LST is the calibration standard - keep efficiency
roughly uniform in eta and pT and let the fake rate vary, rather than applying one global cut to
a score whose meaning drifts with eta. Our banded bars are a crude version of this; a head with
eta as an input plus a WP table is the real version.

## MEASUREMENT PROTOCOL

* Baseline = the window-fix production config (FINDINGS_R2.md). Reproduce its headline numbers
  before trusting any delta.
* Gate every code change: with the new feature block disabled, the binary must reproduce the
  baseline BIT-IDENTICALLY (33/33 branch comparison). State the gate result in your report.
* Price candidate deletions with d1_ref/d1_study.py before spending a 13-minute A/B.
* Confirm anything you recommend on the 977-event sample, and report per-eta-band dup AND fake,
  not scalars. Scalar-only results will be rejected.
* Displaced: report the bands. If they move at all, say so in the headline.

## DELIVERABLE

A retrained head, in the integrated tree, with its gate result and a per-band table vs the
window-fix baseline on 977 events - or a clear negative result with the measurement that shows
it. If the head improves enough that a downstream mechanism can be DELETED, say which one and
price it: that is worth more than the dup delta alone.
