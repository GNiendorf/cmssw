# T4 DNN working point for displaced tracks (ladder rung 4) -- reproducibility

No network was retrained. `t4dnn::runInference` (src/alpaka/NeuralNetwork.h) now accepts a quadruplet when
`fakeScore < kT4FakeWpLoose = 0.99`; the per-(pT, eta) tables `kWp_displaced` / `kWp_fake` of Common.h are no longer read
by it (the scores are still computed and stored). OWED TO THE PHASE-2 RETRAIN.

How the number was obtained (all scripts in this directory; paths in them point at the ladder lane that produced them,
`standalone/displaced_ref/ladder/rungs/r4/design/`):

1. Probe build: `python3 apply_probe.py <worktree>` on the ladder head 2d26b497dd3a (adds the T4 / T5 builder probe,
   `probe_kernels.inc`, `probe_event.inc`), build with `lst_make_tracklooper -C`.
2. Probe run, 1000 events of event_2000 and of event_3000:
   `LST_PROBE_DIR=<dir> LST_MODULE_MAP=<ladder/maps/r2_noskip/...bin> lst_cpu -i <event_N.root> -p 0.8 -n 1000 -s 12 -v 0 -w 0`
3. Truth join: `python3 join45.py t4 <probe dir> <ceiling_tracks.pkl> <event_N.root> <out.npz>` (and `t5`).
4. Scans: `python3 explore1.py <t4.npz> <t5.npz>` (working-point multipliers and alternative forms, track level),
   `python3 scan3.py <t4 e2000> <t5 e2000> <t4 e3000> <t5 e3000>` (the fake-score bound 0.9 / 0.95 / 0.98 / 0.99 / bypass,
   tune and test sample), `python3 predict.py <t4.npz> <t5.npz> t4flag,t4dnn,t4rz` (the rule as deployed).
5. Why the network refuses displaced objects: `python3 dnn_why.py <t4.npz> <t5.npz>` (offline replica `dnnrep.py`,
   weights parsed from the headers; feature-group substitution), `python3 d0true.py t4 <t4.npz>`.

Result used: with the direction flag off and the r-z regions, fake < 0.99 gives track-level T4-or-T5 ownership
0.9986 / 0.9987 / 0.9942 (band / 25-37.2 / 37.2-52.4, event_2000) and 0.9982 / 0.9965 / 0.9963 (event_3000);
fake < 0.9 fails 37.2-52.4 on event_3000 (0.9834). Deployed cost: T4 stored x25.7 per event.
The T5 working point (kWp98) was scanned the same way (x0.01 passes >= 99.6% of true pairs per vxy bin) and is NOT
changed in this commit: deployed, it costs the prompt bin 0.78 pp at track-candidate level.
