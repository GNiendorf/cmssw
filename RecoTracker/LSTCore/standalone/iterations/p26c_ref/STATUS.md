P2.6c milestone status (durable record; all numbers measured, PU200RelVal)

[DONE] M1 incidence compaction implemented + built (7 files, see `git diff --stat`)
       ChainIncidence keyed by DENSE produced index instead of raw module-segmented index.
       Event 0: 12.2 -> 3.1 MB.  10-evt mean: 13.60 -> 3.35 MB.

[DONE] M2 GATE (a) bit-identity, 10 evt --allobj, both backends, vs p26c_ref/bin_before
       -> p26c_ref/fin_bit.out
       CPU : TC 100.0000%, CHAIN(mds) 100%, CHAIN(full) 100%. ntuple 10 ls_* cut-value branches.
       CUDA: TC  99.7035%, CHAIN(mds) 100%, CHAIN(full) 100%. ntuple 326 (baseline GPU nondet).
       CONTROL: same binary run twice also mismatches 11 ls_* branches at the same indices
                (p26c_ref/ctl_cpu_A/B) => those branches are uninitialized-heap garbage in the
                pixel-segment block, i.e. the known -d pLS defect, not this change.

[DONE] M3 repro + stream invariance -> p26c_ref/all_gates.out
       CPU run-to-run 100/100/100. GPU run-to-run chain 100/100, TC 99.7488%.
       CPU vs GPU CHAIN(full) 46.9574% == p26b value exactly.
       -s1 vs -s4: CPU chain+TC 100%, CUDA chain 100%.

[DONE] M4 GATE (c) timing, 20 evt, -s 1, sequential, LST_CHAIN_TIMING=1
       CPU  chain block 53.663 -> 50.824 ms  (-2.839)   NO stage regressed > 0.5 ms
       CUDA chain block  7.642 ->  5.630 ms  (-2.011)   NO stage regressed > 0.1 ms
       K0+K1 incidence: CPU 2.881 -> 1.492, CUDA 3.101 -> 1.099.

[DONE] M5 GATE (b) 300-evt scoreboard, CPU -s 32 -> p26c_ref/gate_b_cpu.out
       AFTER vs BEFORE: rows total 29, rows moved 0, rows identical 29.
       (The 26-moved table is vs the pre-P2.5 frozen prototype and is byte-identical to p26b's,
        i.e. pre-existing drift, not from this change.)

[DONE] M6 P2.7 memory ledger, 10-evt means -> see final report / p26c_ref/ledger/

[DONE] M7 dead-column audit of the 4 chain SoAs (no change applied; findings in final report)

[DONE] M8 grid-item variant MEASURED and REJECTED (CPU +1.319 ms) -> p26c_ref/GRID_DECISION.txt
[DONE] M9 grid variant reverted; rebuilt tree byte-identical to the gated build on lst_cpu,
       lst_cuda, liblst_cpu.so; liblst_cuda.so differs only by nvcc build metadata and was
       re-verified functionally: CHAIN(mds)=CHAIN(full)=100% vs the gated CUDA run.
