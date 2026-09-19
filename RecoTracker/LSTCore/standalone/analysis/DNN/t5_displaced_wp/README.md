# T5 DNN working point lowered for displaced tracks (ladder rung 5) -- OWED TO THE PHASE-2 RETRAIN

No retraining and no new weights: `t5dnn::runInference` (`src/alpaka/NeuralNetwork.h`) accepts a quintuplet at
`dnnScore > kT5WpScale * kWp98[pt][eta]` with `kT5WpScale = 0.01`. A quintuplet that passes only the lowered working point
is marked `heldBack` (QuintupletsSoA): it never removes, extends or blocks a quintuplet above the stock working point and it
is not offered to the pixel match.

The value: the first multiplier at which the network passes >= 99.6% of true quintuplet pairs in every vxy bin on
event_2000 and event_3000 (rung 4 scan):

    displaced_ref/ladder/rungs/r4/design/scripts/scan45.py, dnnrep.py   (offline replica of the network + working-point scan)
    displaced_ref/ladder/rungs/r4/design/out/t5_dnn_scan.md             (the scan table)
    displaced_ref/ladder/rungs/r4/design/DESIGN.md section 3.2          (why the network fails displaced objects)

Deployment of the value with the precedence rule: `displaced_ref/ladder/rungs/r5/design/DESIGN.md` section 6.
Reproduce the patch: `R5_PARTS=t5tier python3 displaced_ref/ladder/rungs/r5/design/patch/apply_r5.py <worktree>`.
