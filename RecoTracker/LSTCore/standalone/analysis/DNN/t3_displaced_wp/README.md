# T3 DNN displaced working point: kT3DispWpScale = 0.02 (ladder rung 3)

No weight was changed and nothing was retrained. The change is one factor on `dnn::t3dnn::kWp_displaced`
(`src/alpaka/NeuralNetwork.h`, `t3dnn::runInference`). This directory holds the scripts and commands that
produced the number, as the DNN reproducibility rule asks for every working-point change.

Why: the stock table is the 99% point of the displaced score over the true displaced (vxy > 0.1 cm) triplets
of stock LST (`train_T3_DNN.ipynb`, last cell). Measured on the unbiased population of the T3 reject probe
(true consecutive segment pairs, including the ones the DNN refuses) it keeps 0.995 at 2.5-10 cm but
0.966 / 0.944 / 0.946 at 10-25 / 25-37.2 / 37.2-52.4 cm, and the loss grows with the impact parameter of
the triplet's own circle (0.997 below 0.5 cm, 0.887 at 8-16 cm).

Inputs: a T3 probe sidecar (`LST_PROBE_DIR`, binary with the T3 reject probe; record layout in `join.py`),
the ceiling truth (`displaced_ref/ladder/rungs/r0/ceiling/ceiling_tracks*.pkl`), the input ntuple.

    # 1. sidecar (1000 events, ptCut 0.8, head map), one per sample
    LST_PROBE_DIR=<dir> LST_MODULE_MAP=<ladder/maps/r2_noskip/module_connection_tracing_merged.bin> \
        lst_cpu -i <event_2000.root> -p 0.8 -n 1000 -s 12 -v 0 -w 0 -o <dir>/debug.root
    # 2. join to truth (local pT at the module, hit positions)
    python3 join.py <dir> <ceiling_tracks.pkl> <event_2000.root> <join_e2000.npz>
    # 3. the scan of the scale, the bypass alternatives, and the per-bin re-derivation with the notebook's method
    python3 dnn_options.py <join_e2000.npz> <join_e3000.npz>
    python3 wp.py <join_e2000.npz> <join_e3000.npz> 0.996 300

Result (both samples pooled, true pairs at local pT >= 0.8, share the DNN passes):

| scale | 2.5-10 | 10-25 | 25-37.2 | 37.2-52.4 | 0.1-2.5 |
|---|---|---|---|---|---|
| 1 (stock) | 0.9952 | 0.9664 | 0.9437 | 0.9463 | 0.9935 |
| 0.05 | 0.9993 | 0.9952 | 0.9941 | 0.9948 | 0.9991 |
| **0.02** | 0.9994 | 0.9977 | 0.9979 | 0.9971 | 0.9994 |

A per-(pT, eta) table re-derived at 99.6% per vxy cell on event_2000 gives the same admission on event_3000 at
the same cost as the single factor, so the single factor was taken. The honest fix is a retrain on the ladder
head's population (its displaced class has never seen a triplet with a widened mini-doublet or a production
radius beyond the first layers); that was NOT done here.
