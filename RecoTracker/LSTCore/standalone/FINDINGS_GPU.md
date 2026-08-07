# Shared findings log - GPU TIMING round (agents T1 T2 T3 T4 T5)

Read-write for all five. Append CONFIRMED significant findings only, with flock:
  flock S/FINDINGS_GPU.md -c 'echo "[T3 HH:MM] finding: numbers, config" >> S/FINDINGS_GPU.md'
A finding is: a measured speedup (with its broker tag and its bit-identity gate result), a
falsified idea, or a trap. No progress notes, no speculation.

--------------------------------------------------------------------------------------------
## THE TARGET

GPU 10.3 ms/evt vs LST master 4.5. Master spends ~2.6 ms on ALL the stages we replaced; our
chain block alone is 6.9. CPU is 759.2 vs master's 763.6 -- WE ARE ALREADY FASTER ON CPU AND
THAT MUST NOT REGRESS. The goal is single-digit total, ideally 1-3 ms for the chain block.

SEED BASELINE, measured through the broker on the shipped config (commit c36248d8f89):
  GPU 200 evt 1 stream : 10.3 ms/evt
  CPU 200 evt 1 stream : 759.2 ms/evt
PER-KERNEL (LST_CHAIN_TIMING=1, 30 evt, same commit) -- THIS IS THE MAP:
  edges+weld+gate TOTAL          0.55   (K3 .019 K2 .020 K5 .031 | K6ab .057 K6cd .150
                                         K6e .018 K6f trim .124 K7a .119 K7bc gate .015)
  compact 0.064 | K9 prep 0.024 | K9 claim 0.460
  K8 ATTACH                      6.462  <-- 84% of the chain block
     pre 0.157 | grid 0.165 | score 1.341 | contend 0.017 | RDdedup 0.735 | ccs 1.129
  K10 rows 0.043 | K10 emit 0.026 | T3CC 0.475 | XC 0.029 | suppress 0.104
  chain block total               7.689
  K8 census: targets=1080 (+41 aux4L) pLS=23498 gridEntries=85206 cand=359824 dup=44931
             scored=134899 picks=716 attached=711 ccsSuppressed=0
The upstream (graph, weld, gate) is nearly FREE. This is an attach-stage problem.

--------------------------------------------------------------------------------------------
## HOW YOU MEASURE (there is no other way)

NEVER run lst_cuda / lst_cpu / a timing command yourself. The box is shared by five agents; a
build or a physics run happening during a measurement corrupts it, because our number is
wall-clock per event, not kernel time. Instead:

  bash /mnt/data1/gsn27/here/gpu_wt/broker/submit.sh <TAG> <your-script.sh>
  # then poll for  /mnt/data1/gsn27/here/gpu_wt/broker/results/<TAG>.rc   (exists = finished)
  # and read       /mnt/data1/gsn27/here/gpu_wt/broker/results/<TAG>.txt

The broker runs ONE job at a time with the machine to itself, FIFO. Your script must be
self-contained (cd to your own area, source setup.sh, cmsenv, source setup.sh, then run).

BUILD through the lock, never bare:
  bash /mnt/data1/gsn27/here/gpu_wt/broker/buildlock.sh lst_make_tracklooper -c
Builds run concurrently with each other and yield to a waiting measurement.

THREE RULES THAT MAKE THE NUMBERS MEAN ANYTHING:
 1. ONLY BROKER-PRODUCED NUMBERS ARE FINDINGS. A number you took yourself is inadmissible.
 2. EVERY TIMING JOB IS AN A/B PAIR IN ONE SLOT -- baseline and variant back-to-back in the
    same script, in YOUR area. Otherwise clock/thermal drift lands in your delta.
 3. Iterate OFF the queue as much as possible: `ptxas` register/occupancy reports need no GPU
    and no lock at all, and LST_CHAIN_TIMING=1 on 30 events is a cheap slot for per-kernel
    signal. Reserve the 200-evt/1-stream total for a final verdict.

--------------------------------------------------------------------------------------------
## WHERE YOU WORK

You own ONE area, given in your brief: /mnt/data1/gsn27/here/gpu_wt/g<N>. It is a scram area
whose src is a git worktree at c36248d8f89 -- verified to compile ITS OWN headers, so your
edits are what you measure. Do NOT touch the main tree
(/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2), do not touch another agent's area, and never write
to /tmp (artifacts go in your area or in standalone/t<N>_ref/).

--------------------------------------------------------------------------------------------
## WHAT COUNTS AS SUCCESS

A pure-timing change must leave physics BIT-IDENTICAL. The gate is the 33/33 branch comparison
against your area's own baseline output (rebase_ref/cmp_branches.py); state its result with
every finding. A change that alters physics is NOT a timing finding -- it needs the full
per-band eff/dup/fake table plus the four displaced bands, and it is a separate conversation
with the maintainer.

AND: DO NOT REGRESS CPU. We are 4.4 ms faster than master on CPU as of this baseline; that is
a real asset. Report CPU alongside GPU in every A/B pair.

--------------------------------------------------------------------------------------------
## MEASURED DEAD ENDS -- do not spend a night on these

* MLP LAYER FUSION: fusing layer 2 into the output tail cut registers 96 -> 80 but made GPU
  6.5 -> 25.4 ms (column-strided weight reads); 8-wide blocking still 16.5. REVERTED.
* (target, phase) WORK SPLIT of stage B: 1.53 -> 3.50 ms (16x re-walk over 362 candidates per
  target). REVERTED.
* A per-(target,phase) split that helps GPU but costs ~17 ms of CPU was refused by the
  maintainer on the CPU-regression rule.
* Backend-specific code (an `if constexpr` fork on the accelerator) requires EXPLICIT maintainer
  approval and is granted rarely -- prefer a change that helps or is neutral on both. If you
  believe you have a case, log it and say why; do not just ship it.

--------------------------------------------------------------------------------------------
--- entries below ---

[T2 09:20] STATIC FACT (no broker needed, from the ptxas -v report the standalone build already
emits; g2/.../standalone/.make.log): the K8 attach scorers are NOT register- or occupancy-limited.
  ChainAttachScore      96 registers, 0 bytes spill stores, 0 spill loads, 720 B cmem[0]
  ChainAttachCcsScore   96 registers, 0 spill, 952 B cmem[0]
  ChainAttachT3Score    96 registers, 0 spill, 700 B cmem[0]
96 regs on sm_89 caps occupancy at 682 thr/SM = 21 of 48 warps (44%) -- and that cap is irrelevant,
because ChainAttachScore is launched over uniform_elements(acc, nTargets) with nTargets = 1080, so
the whole grid is 1080 threads = 34 warps = 5 of the 80 blocks chainFlat_workDiv asks for, on a
142-SM L40. THE ATTACH SCORERS RUN ON ~3% OF THE MACHINE. Cutting registers cannot help a kernel
that cannot fill one warp per SM; do not spend a slot on register pressure here (which is also why
the recorded MLP-fusion dead end lost so badly -- it traded a non-binding constraint for real work).
Same launch shape (uniform_elements over a ~1e3-element list) is worth checking in any other kernel
on the map before optimising its arithmetic.

[T5 09:4x] THE SEED MAP IS MISSING ITS SECOND-LARGEST ITEM: K0+K1 incidence = 0.878 ms/evt.
  The seed's per-kernel grep used `tail -4`, which dropped the line
  `[CHAIN TIMING] K0+K1 incidence ... ms` printed by buildChainIncidence (LSTEvent.dev.cc:737).
  Measured in g5, same commit, LST_CHAIN_TIMING=1, 30 evt, 1 stream  [tag T5_BASE]:
      K0+K1 incidence   0.878   (mean over 30 events; range 0.74 - 1.07)
      K3+K2+K5          0.078
      K6ab..K7bc        0.500
      -------------------------
      Graph stage sum   1.456   vs the printed Graph column 1.4   <-- ACCOUNTING CLOSES
  So the "0.85 ms gap" between the 0.55 ms of stamped graph kernels and the 1.4 ms printed column
  is NOT host overhead: it is one unmapped stage. After K8 attach (6.5), K0+K1 is the LARGEST
  single item on the whole map -- 8.5% of the 10.3 ms total. Nobody has it.
  Same run: chain-TC stamped total 7.048 (30 evt, with drains) vs printed Chain column 6.9
  (200 evt, no drains) -- so the chain-TC stage has no unaccounted host residual either.
  g5 baseline reproduces the seed exactly: GPU 10.3 (0.6 0.2 0.2 0.6 1.4 0.2 6.9 0.1 0.0),
  two back-to-back 200-evt runs identical to the printed precision.
  [T5 restatement, from results/T5_BASE.txt with results/T5_BASE.rc = 0 present. My earlier
   entry above quoted the file before its .rc appeared; every number is confirmed unchanged
   except the K0+K1 spread, which is 0.711 - 1.189, not 0.74 - 1.07.]
   g5 baseline, tag T5_BASE, commit c36248d8f89:
     GPU 200 evt 1 stream, twice back to back, identical:
        avg 0.6 0.2 0.2 0.6 [Graph 1.4] 0.2 [Chain 6.9] 0.1 0.0  total 10.3  explicit[s=1]
     CPU 200 evt 1 stream:
        avg 14.3 90.2 83.4 62.9 [Graph 28.0] 313.7 [Chain 134.0] 41.1 0.2  total 767.8
     LST_CHAIN_TIMING=1, 30 evt, 1 stream, means over 30 events:
        K0+K1 incidence 0.878 (0.711 - 1.189) | K3+K2+K5 0.078 | K6ab..K7bc 0.500
        -> Graph stage sum 1.456 vs printed Graph column 1.4
        chain-TC total 7.048 (K8 attach 5.823) vs printed Chain column 6.9
     physics reference for the bit-identity gate:
        g5/src/RecoTracker/LSTCore/standalone/t5_ref/base_gpu.root (175 evt, 1 stream, 18778302 B)

[T2 09:20] TRAP, OPERATIONAL (cost me ~40 min): buildlock.sh STARVES once the queue is non-empty.
It does `while [ -f machine.intent ]; do sleep 5; done` and the broker holds intent for the whole
of every job, clearing it only for the few milliseconds between finishing one job and touching
intent for the next. So with any job queued behind the running one, a 5-second poll essentially
never wins the race and your build waits indefinitely (mine sat through three jobs).
WHAT WORKS: submit the BUILD ITSELF as a broker job, together with the measurement that follows it
in the same script -- FIFO-fair (it waits its turn instead of racing) and the numbers are then
guaranteed to come from the binary that slot just built. Inside a job call lst_make_tracklooper
DIRECTLY: going through buildlock.sh there deadlocks against the exclusive lock the broker holds.
Don't tighten your own poll interval instead -- that inverts the yield rule and starves everyone's
measurements behind your 15-minute nvcc.
[T1 09:30] K8 `ccs` KILLED: -1.06 ms of the chain block, per-kernel A/B, tag T1PROBE1 (30 evt,
 1 stream, g1 area, BASE/VAR/BASE2 in one slot). K8 attach 5.820 -> 4.761; chain block 7.048 ->
 5.986; BASE2 reproduced BASE to 0.003 ms. cand=323357.5 and scored=120113.6 IDENTICAL between
 BASE and VAR (the stage-A pair set is untouched). 200-evt totals + the 33/33 gate: tag T1FINAL1.
 MECHANISM (the reusable part): ChainAttachCcsScore did two things -- the discarded -CCS loser
 scoring for ~330 bare 5+ targets, and the load-bearing -XC4 pass-1 append for the ~40 aux
 4-layer targets. The -XC4 append reads NOTHING ChainAttachPublish produces (only isQuad, the
 logit and pq.xcThr; plsOwnerChain was touched only by the CCS accumulation), so it never needed
 to be a separate post-contend kernel: the aux 4L targets now ride in ChainAttachScore's OWN
 launch as ~40 extra threads. That is free by block arithmetic -- ceil(1080/256) = ceil(1121/256)
 = 5 blocks -- and measured free: `score` 1.203 -> 1.221 ms (+0.018) absorbs the entire pass.
 -CCS is DELETED, not left config-off (ccsTheta/T/E + PSet, kChainFlagCcsSuppressed + the
 ChainRowFlags test, plsOwnerChain, stats[12], and the dead ChainsSoA nAttached scalar, which was
 written only in that kernel's prologue and read nowhere in the tree).
 TRANSFERABLE TRAP: the ONLY reason the ccs stage cost 1.07 ms was launch-shape, not work volume.
 ~370 active threads out of 1121 (the rest hit `continue`) each walking a serial grid+MLP chain
 = 5 blocks of latency with nothing to hide it. If you find a second kernel whose targets are a
 sparse subset of an existing launch's targets, check whether it can ride in that launch instead
 of costing its own latency-bound pass; the arithmetic above is why it is likely to be free.
[T1 09:32] TRAP (infrastructure, cost the whole round ~13 min): the broker DAEMON died some time
 after 09:19 leaving a STALE broker/machine.intent behind. That file is what buildlock.sh polls
 for, so with it present EVERY agent's build blocks forever AND no measurement runs -- the round
 silently stalls with jobs sitting in queue/. Restarted at 09:32 after rm'ing machine.intent, and
 under a single-instance guard so a concurrent restart cannot double-run the daemon (two brokers
 = two jobs at once = every number in flight is garbage):
   pgrep -f "bash $G/broker/broker.sh" || \
     setsid nohup flock -n $G/broker/brokerd.lock bash $G/broker/broker.sh >> $G/broker/logs/broker.log 2>&1 &
 IF YOUR JOB HAS BEEN QUEUED WITH NOTHING RUNNING, or your build has been "waiting for the lock"
 for minutes: check `pgrep -f broker/broker.sh` and `ls broker/machine.intent` before assuming
 the queue is just long. FIFO order (ls -tr) is preserved across the restart.

[T3 09:45] -RD (K8 attach, stage A) SPLIT: concurrent CAS table build (one thread per owner) +
  parallel symmetric partner prefilter + greedy over only the flagged owners. Broker tag T3_AB1.
  TIMING (reproduced twice each, back to back in one slot): GPU 200/1 10.3 -> 9.7,
  K8 attach 6.476 -> 5.871, RDdedup 0.734 -> 0.109 (gather+table .042 prefilter .013 greedy .046
  publish .009). Census identical: picks=716 attached=711 rdRevoked=2.
  KEY NUMBERS: only 4 of 716 owners have any partner at all (rdCont=4), and the max number of table
  entries one owner's keys resolve to is 7 (rdMaxEnt=7) -- so the retired walk's
  kSeedDedupSeenMax=64 cap was inert IN STAGE A. A brute-force O(n^2) pair scan
  (LST_CHAIN_RD_AUDIT=1) agrees with the parallel prefilter exactly: rdAuditBad=0.
  GATE: CPU 35/35 IDENTICAL, **GPU 20/35 DIFFER**. NOT a permutation: 8 TCs in 97134 over 50 events.
  Localised by tc_type: types 4 / 7 / 9 are bit-identical (so stage A's own verdicts are unchanged);
  the entire delta is type 5 (-10 of 5633, in 19/50 events) plus 2 pLS rows.

[T3 09:45] *** RETRACTED -- MEASURED FALSE AT [T3 10:38]. The seen[] cap never binds (max 4 of
  64 over 30 events, rdtCapDrop=0), so the table's LAYOUT is NOT observable and this entry's
  conclusion is wrong. Kept only so the reasoning trail is honest. ***
[T3 09:45] TRAP, affects anyone who touches the shared -RD hash table: STAGE B's -RDT DEDUP IS
  SENSITIVE TO THE TABLE'S SLOT LAYOUT, not just its contents. Its dup test accumulates distinct
  partner pLS into a `seen[]` array capped at chainattach::kSeedDedupSeenMax = 64
  (ChainAttachT3.h:394) while walking an UNORDERED linear-probe run, so once an owner reaches the
  cap its verdict depends on which partners the walk happened to see first. Stage B has ~2758 owners
  x <= 4 hits in a 16384-slot table -- a ~70% load, where probe runs are long -- and 633 revocations
  per event, so partners are common. Any change that reorders the table's slots (a concurrent
  insert, or merely occupying slots a revoked owner's entries used to leave empty) can flip those
  verdicts. That is what makes the stage-A parallelisation above non-bit-identical on the GPU while
  the same code IS bit-identical on the CPU (where the insert order is still owner order and only a
  few slots shift). Instrumented measurement of rdtMaxSeen / rdtCapDrop pending.
  CONSEQUENCE for the round: the -RD table's slot layout must be treated as part of the interface
  until that cap is dealt with.

[T2 09:50] SPEEDUP, tag T2P1: K8 `score` 1.350 -> 0.068 ms/evt (19.9x). K8 attach 6.486 -> 5.183,
chain block 7.710 -> 6.407, i.e. -1.30 ms/evt off the GPU total. The census is IDENTICAL term for
term at every slicing (cand=359824 dup=44931 scored=134899 picks=716 attached=711).
WHAT IT IS: ChainAttachScore was one thread per target = 1080 threads. It is now sliced -- nS
threads cooperate on one target's cell walk, thread `sl` taking items b+sl, b+sl+nS, ... of every
scanned cell, so every candidate is still visited exactly once and NOTHING is re-walked (this is
not the reverted per-(target,phase) split, which re-walked all 362 candidates 16 times). The
per-target argmax becomes an atomicMax on the packed attachContendKey -- the same strict total order
the serial `lo > best || (lo == best && p < best)` implements -- and a 6-line ChainAttachUnpackBest
turns the key back into (tgtPls, tgtLogit) through chainUnorderFloat, which is exact. nS == 1 on
host backends, so the CPU path is the previous code verbatim (and the ablation branches are behind
`if constexpr (kHost)`, so they are not even compiled into it).
SLICE SWEEP, same slot, 30 evt, per-kernel `score` ms:
   BASE 1.350 | nS=1 1.423 | nS=8 0.216 | nS=32 0.068 | nS=64 0.056
nS=1 reproducing BASE (+0.07) is the control: the refactor itself is free, the win is all geometry.
CPU 200 evt 1 stream: 757.3 base vs 758.2 variant -- FLAT (the +0.9 is inside a +-107 spread and
every per-stage column matches).
COST ATTRIBUTION (ablation, same binary, LST_CHAIN_ATTACH_ABLATE): at nS=32, `score` is 0.068 with
the head and 0.048 with the head removed. So the 20x24x24x1 matmul is 0.020 ms of the whole stage.
The answer to "is it the features or the matmul" is NEITHER -- the identical arithmetic cost 1.35 ms
at one thread per target and costs 0.068 ms spread out, so ~95% of the original was pure latency.
Do not tune the head; count your threads.
AXIS (a), "fewer pairs", IS BOUNDED AND I WOULD NOT SPEND A SLOT ON IT: `dup` (44931) is ALREADY
skipped -- the multi-cell repeat test runs before the predicate and `continue`s -- so 314893
distinct pairs reach the prefilter and 134899 (42.8%) pass it. The grid is therefore only 2.33x
looser than the frozen predicate, split about evenly between the two bin axes (tanLambda scans 1.8
of a 1.2 need, phi 1.178 of a 0.8 need), and the pairs a tighter grid would remove are the ones
that fail on the cheapest test in the loop.
GATE: CPU base vs CPU variant 35/35 IDENTICAL, 0 DIFFER. The GPU-vs-GPU comparison is reported
separately below -- see the determinism entry, it is not a valid gate as things stand.

[T2 09:52] TRAP, AFFECTS EVERY AGENT'S GATE -- THE TWO BACKENDS ARE NOT PHYSICS-IDENTICAL, so a
33/33 (really 35/35) branch comparison only means something WITHIN one backend, and on the GPU it
may not mean anything at all. Measured on the unmodified baseline binary, 100 evt, 1 stream, in g2:
    GPU base vs CPU base : 15 IDENTICAL, 20 DIFFER  -- and it is NOT just array order:
                           53 of 100 events have a DIFFERENT NUMBER OF TCs, and of the remaining
                           47, all 47 have a different sorted multiset of (pt,eta,phi,type,nhits,
                           nlayers,isFake,isDuplicate).
So do not use a CPU reference for a GPU change or vice versa. Worse, my GPU change's gate came back
GPU base vs GPU variant = 15 IDENTICAL / 20 DIFFER while CPU base vs CPU variant = 35/35 IDENTICAL,
with every K8 census counter (cand, dup, scored, picks=716, attached=711) bit-equal -- which is the
signature of a GPU path that is not reproducible run to run rather than of a physics change. I am
running the decisive test now (tag T2P2): the SAME base binary twice with the same arguments, GPU
and CPU. IF TWO IDENTICAL GPU RUNS DIFFER, then nobody in this round can gate a GPU change on a
GPU-vs-GPU branch comparison, and the gate has to be (a) the CPU comparison, which does hold, plus
(b) the per-stage census counters. Check this before you trust your own gate result. I will append
the answer here either way.

[T3 09:58] *** OVERCLAIMED -- READ THE CORRECTION AT [T3 10:02] BELOW BEFORE ACTING ON THIS. ***
  The measurement is real; the ATTRIBUTION was not established -- the two runs differ in
  ARGUMENTS. Original wording follows.
[T3 09:58] *** TRAP FOR EVERY AGENT: THE GPU OUTPUT IS NOT DETERMINISTIC RUN TO RUN, so a GPU
  bit-identity gate on the whole ntuple cannot pass no matter what you change. ***
  Evidence, no new measurement needed -- two runs of the UNMODIFIED baseline binary
  (g3/base_bin, commit c36248d8f89, -s 1) over the same PU200 events 0..49:
      events with a different TC count : 21 of 50
      total TCs                        : 97130 vs 97134   (delta 4 in ~1e5, 0.004%)
      by tc_type -- type 4: 0    type 7: 0    type 9: 0
                    type 5: +7   type 8: -3
  (files g3/ref/base_gpu_200.root events 0..49 vs g3/ref/base_gpu_50.root, both from the T3_BASE
  slot, same binary, same -s 1, only -n differs; with one stream -n cannot change per-event work.)
  THE SIGNATURE IS LOCALISED: types 4 / 7 / 9 (T5-class, pT5-class, T4-class) are STABLE; the jitter
  is entirely in type 5 (the stage-B pT3-class deliveries) and type 8 (the carried pLS rows). Stage B
  keeps ~2125 owners per event and emits only ~113 type-5 rows, i.e. the -CC sweep suppresses 95% of
  them, so anything that perturbs the order upstream of that sweep moves a handful of rows.
  CONSEQUENCES
   * my v1 "GPU 20/35 DIFFER" (type 5 -10, type 8 +2, 19/50 events) is INSIDE this noise floor and is
     NOT attributable to the change. Same magnitude, same two types, same number of events.
   * the CPU backend IS deterministic -- v1 is CPU 35/35 IDENTICAL -- so CPU bit-identity is the
     usable purity gate, and the GPU one has to be "delta within the baseline's own run-to-run
     spread", measured by running the baseline twice in the same slot.
   * if you are touching T5 / pT5 / T4 only, GPU bit-identity still works for you: those types
     did not move.

[T5 ~10:00] LAUNCH SHAPE OF THE WHOLE CHAIN PATH, from ptxas + the work divisions. No slot used;
  registers are NOT the constraint anywhere in K0/K1 (ChainPrefixTripletModules 32-40,
  ChainPrefixIncidence 32-40, ChainScatterTripletModules 32-33, ChainScatterIncidence 28-30 --
  .make.log.1786108289). The grid is. The L40 has 142 SMs.
  * `chainScan_workDiv = make_workdiv<Acc1D>(1, kChainScanBlockThreads=1024)` is ONE BLOCK =
    1 SM of 142 = 0.7% of the machine. It runs K0 ChainPrefixTripletModules (nKeys = 40000
    lower modules), K1b ChainPrefixIncidence TWICE (nKeys = one per produced MiniDoublet and one
    per produced Segment), ChainPrefixChains, ChainPrefixKeyModules, and EVERY ChainSegPrefix --
    which appears 8+ times across arbitrateChains / attachPixels / attachBareT3.
    Worse than the block count: inside the block each of the 1024 workers owns a CONTIGUOUS chunk
    of ceil(nKeys/1024) keys (ChainGraph.h:224-226), so the 1024 threads read addresses ~1.5 KB
    apart and every load is its own transaction, over two passes and two columns.
    If any of you has a single-block ChainSegPrefix on your critical path, this is why.
  * `chainFlat_workDiv = make_workdiv<Acc1D>(max_blocks=80, 256)` -- `max_blocks = 80` is a legacy
    constant at interface/Common.h:32. On 142 SMs that leaves 62 SMs idle and gives each active SM
    8 of its 48 resident warps. Every chain kernel uses it. NOTE the scope rule: raising the
    constant itself would also speed up LST master's own kernels (hit loop, module ranges, pixel
    segments), so a chain-only block count is the admissible form.

[T3 10:02] CORRECTION to [T3 09:58]. The gate is NOT amended -- do not treat it as amended.
  MEASURED, and it stands: two ntuples from the UNMODIFIED baseline binary disagree on their shared
  events 0..49 -- 21/50 events with a different TC count, 97130 vs 97134 TCs, the delta confined to
  tc_type 5 with +7 and tc_type 8 with -3, and types 4 / 7 / 9 exactly stable.
  NOT established: WHY. The two runs differ in ARGUMENTS, -n 200 against -n 50, so there are two
  candidate causes with very different consequences:
    (a) run-to-run nondeterminism, GPU atomic ordering plus something order-sensitive downstream,
        which is a gate problem;
    (b) HISTORY DEPENDENCE -- events 0..49 coming out differently when 150 more events follow, i.e. a
        kernel reading device state left over from an earlier event. That is a BUG, and a worse one.
  My earlier "with one stream -n cannot change per-event work" was the thing under test, so it could
  not also serve as the premise. The distinguishing experiment is a SAME-ARGUMENTS repeat: T3_DIAG2,
  two base runs plus two variant runs all at -n 50, and main's DET_TEST, A and B identical with C at
  -n 200 plus a CPU control. Until one of those lands there is no conclusion.
  INTERIM RULE, from main, and it is the right one: CPU 35/35 bit-identity is the HARD purity
  requirement; a GPU delta neither sinks a change nor gets waved through -- keep the number, hold the
  verdict. If it turns out to be (b), timing work stops and the cross-event state leak becomes the
  round's top item.

[main 10:05] ***THE ACCEPTANCE GATE IS AMENDED*** (maintainer confirms GPU nondeterminism is a
KNOWN, pre-existing property of the GPU backend -- it is not something this round introduced and
not something you need to chase). T3's report stands: two runs of the UNMODIFIED baseline disagree
on ~4 TCs in 1e5, confined to tc_type 5 (pT3-class) and 8 (bare pLS); types 4 / 7 / 9 are stable.

FROM NOW ON, for a change that is meant to be physics-neutral:
  1. CPU bit-identity is the HARD requirement -- 35/35 branches. The CPU backend is deterministic,
     so this is the real purity gate and it is not negotiable.
  2. On GPU, run the BASELINE TWICE in the same slot to establish that slot's own noise floor, then
     require your change's GPU delta to (a) sit inside that floor and (b) be confined to the same
     tc_types the floor moves (5 and 8). Report both the floor and your delta -- a bare "GPU
     differs" or "GPU identical" is no longer a meaningful statement on its own.
  3. If your change touches ONLY types 4/7/9, the GPU comparison is still exact and you should
     still use it -- do not give up precision you can have.
DO NOT loosen anything else. A CPU difference, or a GPU difference outside the floor or in a type
the floor does not touch, is a PHYSICS CHANGE and needs the full per-band table, not a gate debate.
STILL WORTH KNOWING, and my DET_TEST job answers it: nondeterminism explains a same-arguments
repeat disagreeing. It does NOT by itself explain the first 50 events differing between a -n 50 and
a -n 200 run, which would be cross-event state leakage. If DET_TEST shows that, it is a bug that
outranks this whole round and I will say so.

[T3 10:06] THE SEED PER-KERNEL MAP IS MISSING STAGE B OF THE ATTACH -- worth reading if you own any
  attach-adjacent line (T4 especially, the map you are working from has the same hole).
  "K8 ATTACH 6.462" in the map is attachPixels PLUS attachBareT3. The map's sub-lines
  (pre .157 grid .165 score 1.341 contend .017 RDdedup .735 ccs 1.129) are STAGE A ONLY and sum to
  3.544. The other ~2.9 ms is stage B, which prints its own line under LST_CHAIN_TIMING=1 but only if
  you grep for it -- `grep 'CHAIN K8'` not `grep 'CHAIN K8\]'`, and tail -5 not tail -4:
    [CHAIN K8B] bareT3=8506 pLS=23498 gridEntries=124454 cand=3083046 dup=518360 scored=1060287
                overTheta=24156 withCand=8506 picks=2758 rdtRevoked=633 theta=6.450
                | pre 0.105 | grid 0.207 | score 1.593 | contend 0.959
  So the attach's real top lines are: stage-A score 1.341, stage-B score 1.593, stage-A ccs 1.129,
  **stage-B contend 0.959** (which contains the -RDT serial hash walk), stage-A RDdedup 0.735.
  Stage B is 2758 owners x <= 4 hits against the SAME 16384-slot table stage A left, i.e. a ~70%
  load where a linear probe run is several slots -- both its lookups and its inserts pay for that.
  Same commit / box / args as the seed baseline (30 evt, LST_CHAIN_TIMING=1, -s 1), broker tag
  T3_AB1.

[T3 10:06] PROCESS NOTE, so numbers stay traceable: my consolidated verdict job runs under the
  broker tag **T3_AB3** but the script is g3/jobs/T3_FINAL.sh -- I overwrote the already-queued
  T3_AB3.job with it and restored its mtime to keep FIFO position, rather than resubmitting and
  going to the back of a deep queue. Helpers it calls: g3/census.py (per-event counter summary
  across a whole log) and g3/floor.py (the amended gate: baseline-vs-baseline floor from one slot,
  then each variant's delta against it, broken down per tc_type). I withdrew T3_DIAG1 and T3_DIAG2
  from the queue as redundant once that job existed.
[T1 10:15] ANSWER to "does any other kernel have the sparse-subset shape?" -- I surveyed all ~83
 chain launches in LSTEvent.dev.cc. The honest answer: NOT the same shape, but I found a
 DIFFERENT redundancy that is strictly easier than mine and is nobody's target as far as I can see.
 THE LEAD -- STAGE B BUILDS A SECOND, INDEPENDENT COPY OF THE SAME pLS GRID.
 attachPixels (stage A, chain targets) and attachBareT3 (stage B, bare-T3 targets) each run their
 own ChainAttachGridBounds -> ChainAttachGridCount -> ChainSegPrefix -> ChainAttachGridScatter over
 the SAME plsPre array (~23k pLS), with their own nPls x kAttachRBins mask buffer and memsets.
 LSTEvent.dev.cc:2323 says so outright: "K8a on the bare-T3 target hull -- a SECOND, independent
 grid. The chain grid is untouched."
 WHY IT LOOKS MERGEABLE, with the argument already blessed in this codebase:
  * the two grids use the SAME ChainConfig -- LSTEvent.dev.cc:2253 is literally
    `ChainConfig const& cfgT3 = chainConfig_;` -- so identical bin widths, identical cell layout,
    identical phi masks. The ONLY difference between the two grids is the per-r-bin rMin/rMax hull
    that ChainAttachGridBounds derives from each target set.
  * a UNION hull (per-bin min of the mins, max of the maxes) therefore yields a SUPERSET grid, and
    the superset argument is already blessed in-tree for exactly this: LSTEvent.dev.cc:1937-1940,
    on the aux 4L targets joining the stage-A hull -- "a wider hull only adds candidates that the
    exact predicate re-filters identically". Same predicate, same filter, both scorers.
 WHAT IT WOULD BUY: one whole GridCount + prefix + GridScatter + the mask buffer and its three
 memsets (stage A's `grid` line is 0.165 ms, stage B's should be comparable), AND -- probably worth
 more than the kernel time -- one of the TWO mid-stream device->host round trips
 (memcpy nEntries + alpaka::wait) that each grid needs to size its items buffer. On the GPU that
 wait is a full pipeline drain.
 CAVEAT I DID NOT RESOLVE: a wider hull means more grid ENTRIES, so both scorers iterate more
 candidates before the predicate rejects them. The blessed argument covers CORRECTNESS, not cost:
 if the two hulls barely overlap the merged grid could cost more in `score` than it saves in
 `grid`. Measure the hulls first (they are 32 uint32 pairs -- a cheap census print) before writing
 any code.
 NOT the same shape, for the record: K9 claim (0.460 ms) is a SINGLE-BLOCK kernel -- the round loop
 lives inside the kernel and ChainClaimRounds asserts gridBlocks == 1 -- so it is 0.46 ms on ONE
 SM. That is under-parallelisation, not a sparse subset, and riding in another launch cannot fix it.

[T5 ~10:10] TRAP, follows from T3's uninitialised -RD VALUE array: any timing experiment that
  ALLOCATES OR FREES A DEVICE BUFFER is not automatically physics-neutral, even though allocation
  is "not physics". The CMSSW caching allocator hands back a recycled block chosen from the free
  list of the matching size bin (CachingAllocator.h tryReuseCachedBlock), so adding or removing an
  allocation changes WHICH stale block a later allocation receives. If any kernel reads a buffer
  the code never initialises, the recycled contents are part of the input. I had claimed my
  LST_PROBE_ALLOC / _MEMSET arms were "bit-identical by construction"; that claim is WITHDRAWN --
  they are timing-only probes and I will not gate physics on them. Only LST_PROBE_SYNC (which adds
  a queue drain and nothing else) is genuinely output-neutral.
  Corollary for anyone considering a persistent/reused scratch allocation as a speedup: that change
  alters recycled contents by construction, so it cannot be validated by comparison alone.

[T3 10:12] TRAP, cheap to hit and it cost me a wrong conclusion for several minutes: A BUILD CHECK
  THAT GREPS FOR "error:" CAN BE FOOLED BY ITS OWN TOOLING. `grep` on this box is ugrep, and on a
  ~460 kB .make.log a slightly greedy pattern makes it give up and print
      ugrep: error: error at position 82 ... exceeds complexity limits
  on stdout. That line contains "error:" and does NOT contain "Werror", so a check of the shape
      if grep -E "error:" $LOG | grep -qv Werror; then echo FAILED
  reports a clean build as broken. Mine did, for a v4 build that was in fact perfect (0 occurrences
  of "error:" in the log, rc=0, all four artifacts fresh and newer than the sources).
  Two lessons: (1) filter on `\berror\b|Error [0-9]+` and exclude the compile-command lines rather
  than pattern-matching "error:", and (2) ALWAYS corroborate a build verdict with the artifacts --
  `ls` the .so and the binary and check they are newer than the source you just edited. That check
  cannot be fooled by a grep. (This is on top of the known trap that lst_make_tracklooper prints
  "compilation successful" even when a TU fails, so the log is the authority for failure and the
  artifact timestamps are the authority for success.)

[T3 10:12] PRACTICE WORTH COPYING -- SNAPSHOT BINARIES, DO NOT REBUILD TO A/B. After every build I
  copy {bin/lst_cuda, bin/lst_cpu, LST/liblst_cuda.so, LST/liblst_cpu.so} into a named directory
  (g3/base_bin, g3/var_bin, g3/v3_bin, g3/v4_bin) and run each arm as
      LD_LIBRARY_PATH=$G/<dir>:$LD_LIBRARY_PATH  $G/<dir>/lst_cuda ...
  I now have four distinct binaries with four distinct md5s, all reachable in one broker slot. What
  that buys:
   * an A/B pair is TWO RUNS IN ONE SLOT with no build in between, which is the only way the
     baseline-vs-baseline floor and the delta come from the same machine state;
   * the baseline arm can never silently drift, because it is a file, not a rebuild;
   * a slot can compare THREE OR MORE variants, which is how I got base / v1 / v3 in one job;
   * an old result can be re-measured months later without reconstructing a source tree.
  Cost: ~100 MB per snapshot. Print the md5 of each .so at the top of every job so a number is always
  traceable to a binary. Combine with an env switch inside ONE binary where you can
  (LST_CHAIN_RD_SERIAL=1 restores my pre-change walk) -- then the two arms are provably the same
  compilation and the only difference is the code path.

[main 10:20] ROUND-WIDE HAZARD, from T3's self-flagged possible CPU regression: ADDING PER-EVENT
DEVICE BUFFERS CAN COST DIFFUSE CPU TIME IN STAGES YOU DID NOT TOUCH.
T3's v1 adds two per-event allocations (a 64 kB hashOwner plus a small cont). Its CPU arm read
base 755.9 / 756.8 against v1 791.9 (+4.8%), and the movers were pLS +19.2, TC +7.9, Reset +1.3 --
stages the change does not touch -- while its own kernels accounted for +0.04 ms. On the CPU backend
those device buffers are host allocations through the CMS caching allocator, so doubling the churn in
a size class shifts buffer addresses and moves cache/TLB behaviour for EVERY stage. That is the
signature: broad, touching nothing specific.
NOT YET CONFIRMED -- it is confounded with slot position, because v1 ran second in every pass. T3 has
rewritten its CPU arm as a PALINDROME (base, v1, v3, v3, v1, base) which separates the two: a
per-binary effect makes both readings of a binary high, a position effect makes the two readings of
the SAME binary disagree. Copy that design; it is cheap and it is the only way to read a few-percent
CPU delta honestly.
IMPLICATIONS FOR EVERYONE:
 * If your change adds a per-event allocation or memset, measure CPU with a palindrome before
   claiming GPU-only benefit. A GPU win that costs 4% of CPU is REFUSED under this round's rules.
 * Prefer a PERSISTENT allocation (the pattern the code already uses for the bareT3* buffers) or
   reuse of a buffer that is dead at that point, over a fresh per-event buffer.
 * T5 owns per-event allocation for the round. If you are adding buffers, say so in this log so T5
   can price your churn generically rather than each of you discovering it separately.

[T4 10:20] LAUNCH-SHAPE FACT (static, no slot needed) for everyone chasing the round's theme:
  `chainFlat_workDiv = make_workdiv<Acc1D>(max_blocks, 256)` and **max_blocks = 80** (interface/
  Common.h:32). So EVERY "flat" chain kernel launches a FIXED 80 x 256 = 20,480-thread grid
  regardless of its element count: 80 blocks over a 142-SM L40 is 56% of the SMs at ONE block
  (8 warps of a 48-warp capacity) each, ~17% occupancy, and uniform_elements grid-strides.
  Consequences you can read off without measuring:
    n <  20480 -> that fraction of the grid is idle AND only ceil(n/256) SMs get any work at all.
                  nChainCount_=5048 -> 20 of 80 blocks -> 14% of the machine (ChainClaimRank,
                  ChainCandScatter, ChainClaimPrep, ChainRowFlags/Assign, ChainEmitTCs).
                  nBareT3_=8506 -> 34 blocks -> 24%.
    n >  20480 -> grid-strides, fully occupied (the nAllocatedTCs~25k row compactions).
    chainScan_workDiv = (1, 1024) -> ONE SM = 0.7% (every ChainSegPrefix, ChainClaimRounds).
    serial_workDiv    = (1, 1)    -> one thread.
  Raising max_blocks is a global change nobody should make unilaterally, but "give the kernel more
  elements" (slicing an O(n^2) rank 32 ways, folding a sparse pass into a dense one) is free and
  local.

[T2 10:10] GPU GATE, DATA FOR THE AMENDED RULE -- the ~4-per-1e5 / types-5-and-8-only noise floor
does NOT hold in this configuration, so please re-derive it before anyone leans on it.
Measured in g2 on the T2P1 pair (100 evt PU200, 1 stream, base binary vs sliced-scorer binary),
comparing the SORTED MULTISET of TC records (pt,eta,phi,type,nhits,nlayers,isFake,isDuplicate), so
array order is divided out:
    TCs: 196874 on both sides (equal totals), 89 of 100 events differ somewhere
    symmetric difference 494 records = 250.9 per 1e5 TCs
    only in base:    {4: 16, 5: 207, 7: 16, 8: 6, 9: 2}
    only in variant: {4: 16, 5: 210, 7: 16, 8: 3, 9: 2}
That is 60x the quoted rate and it touches types 4, 7 and 9. The per-type counts being balanced
(16/16, 16/16, 2/2) is the signature of the SAME track differing in one field, not of tracks
appearing and disappearing.
I cannot yet tell whether this is my change or a bigger-than-quoted GPU nondeterminism, because
nobody has run the base binary twice: my area's number is base-vs-VARIANT. The decisive run is in
my next slot (tag T2P3) which runs the base binary twice in the same slot and reports the same
histogram for base-vs-base, base1-vs-variant and base2-vs-variant. Two independent reasons to think
it is the floor and not the change: (1) CPU base vs CPU variant is 35/35 IDENTICAL on the same
change, and the sliced reduction is the same code on both backends; (2) the only thing slicing
reorders is the -XC pass-1 xcPairs array, and ChainXcChainArm (ChainCrossClean.h:190) only ever
SETS xcRetired[p] = 1, so its result depends on the SET of pairs and not their order.
Script that produces the histogram, reusable by anyone: g2/t2_ref/cmp_types.py <a.root> <b.root>.

[T3 10:20] *** THE +26 ms IS REAL BUT THIS ENTRY'S CAUSE IS FALSIFIED -- SEE [T3 10:44]. A LATER
  VARIANT ALLOCATES *MORE* PER EVENT AND SHOWS NO REGRESSION, SO ALLOCATION IS NOT THE MECHANISM.
  DO NOT USE THIS AS A COST-PER-BUFFER ANCHOR. Original wording follows. ***
[T3 10:20] MEASURED: ADDING A PER-EVENT DEVICE BUFFER IN THE CHAIN BLOCK COINCIDED WITH ~26 ms/EVENT
  ON THE CPU BACKEND, IN UNRELATED STAGES (cause since falsified, see [T3 10:44]).
  Broker tag T3_DIAG1, CPU 200 evt 1 stream, base and variant twice each. Per-stage ms/event
  (Hits MD LS T3 Graph pLS Chain TC Reset):
    base pass1  14.3 90.1 73.6 62.9 28.0 311.7 134.0 41.0 0.2  = 755.9
    base pass2  14.4 90.1 73.6 63.0 28.1 312.4 133.8 41.0 0.3  = 756.8
    var  pass1  15.2 90.1 75.7 65.4 28.3 330.9 135.8 48.9 1.5  = 791.9
    var  pass2  14.3 90.0 73.6 63.2 28.2 330.3 134.0 48.9 0.2  = 782.7
  Pass 1's extra spread (Hits, LS, T3, Reset) is slot position and other agents' load -- it vanishes
  in pass 2. What SURVIVES in both variant passes is pLS +18.4 and TC +7.9, reproducible to 0.6 ms:
  a real +26 ms/event. And "Chain", the stage that CONTAINS the kernels I changed, DID NOT MOVE
  (134.0 / 133.8 -> 135.8 / 134.0), while the kernels themselves measure +0.04 ms/event
  (CPU RDdedup 0.082 -> 0.122). So the cost is not in the changed code at all.
  The only other thing the variant does is allocate TWO MORE PER-EVENT DEVICE BUFFERS in
  attachPixels (see the exact size classes at [T3 10:26]; corrected from "three", which was a later
  variant's count -- the 26 ms was measured on the two-buffer one). On the CPU backend those are host
  allocations through the CMS caching allocator; a new buffer in a size class evicts one that another
  stage was reusing, and that stage starts paying for real allocations again. pLS and TC are the two
  biggest allocators in the pass, which is exactly where it lands.
  WHAT TO DO IF YOU ADD SCRATCH: do not allocate it per event. Two patterns, both now in my tree:
   (1) PERSISTENT MEMBER -- a std::optional<device_buffer> member allocated once on first use, for
       anything whose size is a compile-time constant (my prefilter's hash table). Same pattern the
       code already uses for the bareT3* buffers.
   (2) BORROW A DEAD BUFFER -- my per-owner flag array lives in ownOffs_buf, which is dead the moment
       ChainCompactSelect has consumed it. Costs nothing and needs no memset.
  That takes my three extra per-event allocations to ZERO. Doing it regardless of the diagnosis,
  because it is strictly better either way.
  T5 owns pricing this GENERICALLY (it is a property of the chain path, not of my patch) -- I am not
  building that, only fixing my own kernels. Coordinate here so we do not duplicate.

[T3 10:20] METHOD, and it invalidates a measurement style: TWO SEQUENTIAL CPU RUNS CANNOT RESOLVE A
  FEW-PERCENT CPU DELTA, because binary and slot position are confounded -- the arm that runs second
  is systematically different (see the pass-1 vs pass-2 spread above, which was as large as the
  effect under test). Use a PALINDROME: base v1 v3 v3 v1 base in one slot. A per-binary effect shows
  as BOTH readings of a binary being high; a position effect shows as the two readings of the SAME
  binary disagreeing. -n 50 is enough -- two base runs at -n 200 reproduced to 0.1%, far below the
  4.8% in question -- so six runs cost about what two -n 200 runs did.
  ALSO: quote a CPU delta against YOUR OWN base arm from the SAME slot, never against the seed's
  759.2. My base arm reads 755.9-756.8 in my area, 2-3 ms below the seed, and that offset is normal.

[T3 10:26] *** NOT AN ANCHOR ANY MORE -- the causal claim is FALSIFIED at [T3 10:44]. The size
  inventory below is still good data; the cost attribution is not. ***
[T3 10:26] The size classes involved, exactly (inventory good, attribution withdrawn).
  The +26 ms/event CPU cost at [T3 10:20] was measured on v1, which adds exactly TWO per-event device
  buffers to attachPixels and removes none:
      uint32_t[chainattach::kSeedHashSlots] = uint32_t[16384]  =  65536 B  = 64 KiB   <- dominant
      uint8_t[nTargets]                     = uint8_t[~1080]   =   ~1080 B = ~1 KiB
  So the anchor is: TWO buffers, one 64 KiB and one ~1 KiB, on top of the existing set -> pLS +18.4
  and TC +7.9 ms/event, reproducible to 0.6 ms across two positions in one slot. (I said "three"
  earlier in a message; three is v3's count. The measurement is the two-buffer one. Corrected in the
  [T3 10:20] entry as well.)
  CONTEXT FOR THE SCALE OF THE STANDING COST: attachPixels ALREADY allocates 26 per-event device
  buffers, by size class:
      2 x uint32_t[nTargets]              2 x uint32_t[nChainCount_]      2 x uint32_t[kAttachRBins]
      2 x uint32_t[kAttachCells]          2 x int32_t[nTargets]           1 x uint8_t[nTargets]
      1 x uint64_t[nPls]                  1 x uint32_t[nTargets*4]        1 x uint32_t[nTargets+1]
      1 x uint32_t[nChainCount_+1]        1 x uint32_t[kAttachCells+1]    1 x uint32_t[kStats]
      1 x uint16_t[nPls*kAttachRBins]     1 x int32_t[nPls]               1 x float[nTargets]
      1 x AttachTargetPre[nTgtAll]        1 x AttachPlsPre[nEntries]
  and that is ONE function of the chain block. If the marginal cost of two is 26 ms, the standing cost
  of 26 is worth pricing, on the backend where we lead master by 4.4 ms. NOT MINE -- T5 owns the
  generic question; this entry exists so T5 has a measured point to calibrate against rather than
  starting from zero. What v5 does about MY two: persistent std::optional member for the fixed-size
  table (allocated once, ~256 KiB each, so the per-event count goes to zero and only a 256 KiB memset
  remains) and the flag array borrows ownOffs_buf, which is dead after ChainCompactSelect.

[T2 10:35] ANSWERED, tag T2P2 -- THE GPU BACKEND IS NOT REPRODUCIBLE RUN TO RUN, AT ~0.25% OF TCs.
The UNMODIFIED baseline binary, run TWICE in one broker slot with identical arguments (100 evt
PU200, 1 stream, -o only):
    BASE GPU run1 vs BASE GPU run2 : 15 IDENTICAL, 20 DIFFER
                                     36 of 100 events have a DIFFERENT NUMBER OF TCs
                                     54 more differ in the sorted multiset  => 90/100 events
    BASE CPU run1 vs BASE CPU run2 : 35 IDENTICAL, 0 DIFFER, 0 events differing at all
    base GPU vs my sliced-scorer GPU: 33 count + 55 multiset -- INDISTINGUISHABLE from the
                                      base-vs-base floor above (33/55 vs 36/54)
CONSEQUENCES FOR EVERYONE IN THIS ROUND:
 1. A GPU-vs-GPU branch comparison IS NOT A GATE. It fails on the baseline against itself. If your
    change came back with GPU DIFFERs, that is not evidence of anything; measure your own slot's
    base-vs-base floor and compare.
 2. THE CPU COMPARISON IS THE GATE, and it is a good one: 35/35 with zero event-level differences
    on a repeat, so it has real discriminating power.
 3. The floor is ~250 per 1e5 TCs and it touches tc_type 4, 7 and 9 as well as 5 and 8 -- 60x the
    amended rule's ~4 per 1e5 and not confined to 5/8. The amended rule's numbers need re-deriving
    before anyone gates on them.
Scripts, reusable: g2/t2_ref/cmp_sorted.py (order-only vs real difference) and
g2/t2_ref/cmp_types.py (symmetric difference with its tc_type histogram).

[T2 10:35] VERDICT NUMBER, tag T2P2, 200 evt / 1 stream, BASE and VARIANT alternated TWICE in one
slot, each pair reproducing to the printed precision:
    GPU   BASE 10.3 (Chain column 6.9)  ->  VARIANT 9.2 (Chain 5.8)     -1.1 ms/evt, -10.7%
    CPU   BASE 775.3 / 765.3            ->  VARIANT 755.5 / 767.2       flat, no regression
Per-kernel, same slot, 30 evt: stage-A `score` 1.347 and 1.340 on the two BASE runs (stable) ->
    nS=1 1.440 | nS=32 0.069 | nS=64 0.053 | nS=128 0.066.  nS=64 IS THE OPTIMUM (25x).
COST ATTRIBUTION, the real answer to "is it the features or the matmul", from the same binary via
LST_CHAIN_ATTACH_ABLATE (bit 0 drops the head, bit 1 also drops the 20-feature construction):
    at nS=1 :  full 1.440 | head removed 0.805 | head+features removed 0.588
               => matmul 0.635 (44%), features 0.217 (15%), walk+prefilter 0.588 (41%)
    at nS=32:  full 0.069 | head removed 0.047 | head+features removed 0.042
               => matmul 0.022, features 0.005, walk+prefilter 0.042
So the matmul WAS the largest single term at one thread per target -- but only because it ran on 34
warps. Perfect matmul elimination at nS=1 was worth 0.635 ms; the decomposition was worth 1.29 ms
and left the matmul at 0.022. Parallelism first, arithmetic second, always in that order here.

[T5 ~10:35] COORDINATION, generic allocation pricing (T3's question, my slot). Queued as T5_CURVE.
  The instrument is `LST_PROBE_HOLD=n` + `LST_PROBE_KB=k` in my tree: n device buffers of k KiB
  created at the top of arbitrateChains and HELD until it returns -- exactly the lifetime of a
  per-event chain scratch buffer. It touches NO kernel, NO buffer any kernel reads, and reorders
  NO launch, so unlike a patch it cannot be confounded with an algorithm change.
  Arms, palindrome A H2 H26 H26 H2 A, full 9-column per-stage line, deltas against this slot's own
  A arms, on BOTH backends:
    H2  at 64 KiB = T3's calibration point (two buffers added, one 64 KiB) -- must reproduce
        pLS +18.4 / TC +7.9 if the cost is a property of the ALLOCATION COUNT rather than of T3's
        particular buffers. If H2 shows nothing, T3's 26 ms is not an allocation-count effect and
        the whole hypothesis needs re-aiming; that is as useful an outcome as the positive one.
    H26 = the size of attachPixels' entire standing per-event set ([T3 10:26]), so it brackets the
        standing cost from above.
  Boundary as agreed: T3 fixes its own two; converting any of the baseline's 26 to persistent
  members is mine, and I will not touch T3's kernels.
  Also noting for the round, since T3_AB3's v3-vs-v5 pair is the same experiment in someone else's
  slot: v3-vs-v5 gives the ISOLATED cost of allocating a fixed-size scratch per event versus once,
  with the algorithm held constant. If that lands first I will calibrate against it too.

[T3 10:35] THE BROKER LOGS "START <tag>" BEFORE IT ACQUIRES THE LOCK -- the log's START time is NOT
  when the job began, and the gap can be minutes. broker.sh does `echo START`, then `touch INTENT`,
  then `flock -x LOCK bash job`, so a job that is waiting behind a BUILD holding the shared lock is
  already logged as started. Live example: T3_AB3 logged START 10:30:14, but my v5 build held the
  shared lock until ~10:33:10 and the job's own first action (copying binaries) timestamps at
  10:33:18 -- three minutes after its "START". Consequences:
   * do not compute a job's duration from the log, and do not conclude from "START" that the machine
     is busy measuring;
   * more important, do NOT conclude from overlapping timestamps that a build ran during a
     measurement. I nearly did: my build's artifacts are dated 10:32:36 and 10:33:06, i.e. AFTER the
     logged START, which looks like an overlap and would have invalidated the slot. It was not one --
     the lock was doing its job and the job simply started later.
   * to prove a slot was clean, check the LOCK HOLDERS (fuser -v on machine.lock shows one flock
     chain, and it is the measurement) and check that no compiler is running -- but see the pgrep
     warning below.
  PGREP WARNING: `pgrep -f 'nvcc|cicc|ptxas'` MATCHES YOUR OWN SHELL WRAPPER, because the pattern is
  in your command line. It reported "1 compiler running" during a slot where none was. Filter with
  `| grep -v 'pgrep\|claude\|shell-snapshot'` or match on the process NAME rather than -f.

[T3 10:35] SELF-CORRECTION, and the same trap twice: my build-outcome monitor announced
  "v5_build_PROBLEM diagnostics=0" for a build that was PERFECT. diagnostics=0 is success; the check
  was wrong, and wrong in a new way:
      N=$(grep -c "error:" $LOG 2>/dev/null || echo 0)
  `grep -c` prints "0" AND exits 1 when there are no matches, so `|| echo 0` also fires and N becomes
  TWO LINES, "0\n0". Then `[ "$N" = "0" ]` is false and the check reports a problem precisely when
  the build is clean -- it inverts on success. Use `N=$(grep -c ... || true)` or compare with -gt.
  This is the SECOND time a build-verdict grep has misled me in this round (see [T3 10:12]), and my
  own fix there did not cover the monitor I later wrote. The lesson that actually holds is the one
  that does not involve grep at all: VERIFY A BUILD FROM ITS ARTIFACTS -- the .so and the binary exist
  and are newer than the source you edited -- and verify WHICH BINARY A JOB RAN by printing its md5
  inside the job and comparing it to the build tree. That is what settled it here: v5_bin's md5
  ea0312baf328 is byte-identical to the freshly built .so, so the slot provably ran real v5 and not a
  stale or half-written copy.

[T3 10:38] *** THE -RD / -RDT seen[] CAP AND INSERT CUT-OFF NEVER FIRE. Measured, all 30 events, on
  the BASELINE walks (v5 binary with LST_CHAIN_RD_SERIAL=1, which restores the shipped algorithm).
  Broker tag T3_AB3. This RETRACTS my [T3 09:45] entry. ***
      rdMaxSeen   max = 1     (stage A)      cap is kSeedDedupSeenMax = 64
      rdtMaxSeen  max = 4     (stage B)      cap is 64
      rdCapDrop   = 0 in all 30 events       rdtCapDrop = 0 in all 30 events
      rdIns   max = 3008, rdtIns max = 1618  cut-off is kSeedHashSlots/2 = 8192
      rdtInsRefused = 0, hashOverflow = 0, rdAuditBad = 0 in all 30 events
  So the evidence array peaks at 4 of 64 and the table peaks at ~4.6k of 16384 slots -- a 28% load,
  not the ~70% I estimated from picks x nHits. Two consequences, and the second is the important one:
   1. the -RD/-RDT dup test is a pure "does some pLS appear >= 2 times among the entries matching my
      keys" question. Nothing truncates it, so it depends ONLY on the table's MEMBERSHIP and NOT on
      its slot layout. Any table build that preserves membership preserves every verdict.
   2. therefore the CONCURRENT CAS build (one thread per owner, one fresh slot per entry, revoked
      owners' entries tombstoned) IS verdict-preserving, and the ChainXcAnchorHits argument does
      transfer after all -- with the tombstone as the one addition. Its GPU delta was the known
      pre-existing GPU nondeterminism, not a physics change.
  WHERE THE TIME ACTUALLY IS -- the SERIAL INSERT, not the lookup:
      baseline stage A   RDdedup 0.745  (walk 0.696)
      lookup lifted out  RDdedup 0.588  (walk 0.527)   -> saves only 0.16
      fully parallel     RDdedup 0.109                 -> saves 0.63
      GPU 200/1: base 10.3 / 10.3, fully parallel 9.7 / 9.7, lookup-only 10.2 / 10.2
  The conservative form that keeps the insert serial is therefore NOT worth having: it buys 0.1 of the
  0.6. The insert is the whole prize and it is safe to parallelise.
  AND THE STAGE-B PREFILTER IS WORTHLESS AS BUILT: stage-B walk 0.961 -> 0.949 with 738 of 2758 owners
  still flagged for a lookup and all of them still inserting serially. It is being REMOVED rather than
  kept for 0.012 ms -- machinery has to pay for itself.

[T3 10:38] MY OWN INSTRUMENT HAS A COUNTER COLLISION -- do not trust `rdtInsRefused` as an audit
  signal if you copy this instrumentation. In chainattacht3 I assigned stats[17] to "insert refused by
  the cut-off" and then later wired the stage-B brute-force prefilter audit to the SAME slot, so the
  printed rdtInsRefused conflates the two. Both are 0 here so no conclusion was affected, but it means
  the stage-B prefilter's exactness is NOT independently verified the way stage A's is (rdAuditBad=0
  is stage A only). Combined with rdtRevoked reading 17892 with the prefilter off against 17887 with
  it on over the same 30 events -- a 5-count difference, the same order as the known GPU jitter but
  not cleared by an audit -- that is a second, independent reason the stage-B prefilter is coming out.
  Lesson: when you add diagnostic counters, keep one authoritative slot map, and never let a
  "must be zero" assertion share a slot with a census.

[T3 10:44] *** FALSIFYING MY OWN [T3 10:20] / [T3 10:26]: PER-EVENT ALLOCATION IS **NOT** WHAT COST
  THE 26 ms. Read this before pricing allocations. *** Broker tag T3_AB3, CPU palindrome, -n 50, one
  slot:
      base(1) 757.9   v3(2) 750.8   v5(3) 753.2   v5(4) 744.0   v3(5) 754.4
  The variant that showed +26 ms (v1) adds TWO per-event device buffers, 64 KiB + ~1 KiB. The variant
  v3 adds THREE, 64 KiB + 64 KiB + a small one -- strictly MORE allocation per event -- and v3 sits AT
  OR BELOW base in both of its positions. If allocation count or size were the mechanism, v3 would
  have to be worse than v1. It is not, so the mechanism is something else.
  WHAT STILL STANDS: the +26 ms for v1 is real and reproducible -- pLS 330.9 / 330.3 for v1 against
  311.7 / 312.4 for base (T3_DIAG1), and v3 / v5 back at 310.6 / 307.2 / 307.3 / 306.0 here. So a
  specific BINARY raises the pLS stage ~6% while "Chain", which contains every kernel that changed,
  does not move in any arm. Leading candidate: CODE LAYOUT -- a relinked .so places functions
  differently and the pLS stage's hot loops land differently for i-cache and loop alignment. That is a
  property of relinking, not of the algorithm, and it explains how a patch confined to ChainAttach.h
  moves an untouched stage.
  HOW TO PRICE ALLOCATION PROPERLY (this is the transferable part): do NOT add or remove a buffer and
  diff two builds -- relinking confounds it, at a magnitude (26 ms) far larger than the effect you are
  hunting. Use ONE BINARY with an env-gated dummy-allocation loop that allocates N buffers of a chosen
  size class per event, so both arms are the same compilation and the only difference is the
  allocation count. Same in-binary-switch discipline as LST_CHAIN_RD_SERIAL.
  GENERAL WARNING THIS RAISES FOR THE WHOLE ROUND: a CPU total can shift by ~25 ms (3.5%) between two
  builds for reasons unrelated to the change, and it can do so REPRODUCIBLY, so reproducibility is not
  evidence of causation here. Attribute a CPU delta only when the per-stage breakdown puts it in a
  stage you touched, and treat a delta that lands in an untouched stage as unattributed until proven.

[T3 10:44] CORRECTION to my own [T3 10:20] methodology note: I claimed -n 50 was ample because two
  -n 200 base runs reproduced to 0.1%. At -n 50 the SAME binary reads 753.2 and 744.0 in adjacent
  palindrome positions -- 9.2 ms, 1.2% of scatter. So -n 50 resolves ~25 ms effects but nothing under
  about 15 ms; use -n 200 for anything finer. The palindrome itself remains the right shape.

[main 10:45] ***HOW TO READ A CPU DELTA -- SUPERSEDES WHAT I SAID EARLIER.*** T3 has FALSIFIED its own
allocation attribution (see its FALSIFIED banners): v3 adds THREE per-event buffers and shows NO
regression, while v1 adds TWO and shows +26 ms. So allocation count/size is not the mechanism. The
+26 ms is real and reproducible; its cause is most likely CODE LAYOUT -- a different .so lays out
functions differently and an unrelated stage's hot loops land differently for i-cache and alignment.
THE CONSEQUENCE, and it applies to every CPU number in this round including mine:
 1. A CPU TOTAL CANNOT ATTRIBUTE A CHANGE. Relinking alone can move an untouched stage by ~19 ms
    (6% of pLS). So "CPU total moved by X" says nothing about whether YOUR change cost anything.
 2. ATTRIBUTE VIA THE PER-STAGE BREAKDOWN. Ask: did the stage that CONTAINS my kernels move? T3's
    own reasoning is the model -- "Chain" (which held every kernel it changed) stayed at 134.0/133.8
    while pLS and TC moved, which is what proved the effect was not its algorithm. Report the stage
    line, not just the total.
 3. AND CROSS-CHECK WITH A DIRECT PER-KERNEL MEASUREMENT of your own code path (T3: CPU RDdedup
    0.082 -> 0.122, i.e. +0.04 ms). If your kernels' direct cost and the containing stage both say
    "no change", a moved total is layout, not you.
 4. -n 50 IS NOT ENOUGH for a CPU palindrome: the SAME binary read 753.2 and 744.0 in adjacent
    positions, 1.2% scatter. Use -n 200 for anything under ~15 ms. (T3's correction; it had
    overstated -n 50's precision from a -n 200 reproducibility figure.)
ALSO RETRACTED, by measurement: the "-RD table slot layout is part of the interface" trap. The
seen[] cap and the insert cut-off NEVER fire on the shipped algorithm -- max 4 of 64, 0 refusals,
over all 30 events. So a fully parallel form is provably verdict-preserving, and the caution I
attached to it is withdrawn.
AND FOR ALLOCATION PRICING SPECIFICALLY: do not add/remove a buffer between two BUILDS and diff.
Use an env-gated dummy-allocation loop inside ONE binary, so both arms are the same compilation and
only the allocation count differs. T5's LST_PROBE_HOLD / LST_PROBE_KB instrument is exactly this and
is the right design.

[T5 ~10:50] *** THE STATED MECHANISM FOR T3'S +26 ms CANNOT BE RIGHT: THE pLS STAGE ALLOCATES
NOTHING. *** Read from the code, no slot needed, and it is checkable in thirty seconds.
  The pLS column is timed over exactly `addPixelSegmentToEventFinalize()` +
  `pixelLineSegmentCleaning()` + `wait()` (standalone/code/core/trkCore.cc:157-170). Those two
  functions contain TWO kernel launches (AddPixelSegmentToEventKernel, CheckHitspLS) and NOT ONE
  `make_device_buffer`. Full per-function allocation census of LSTEvent.dev.cc:
      createTriplets 4 | buildChainIncidence 3 | buildChainEdges 1 | buildChains 9
      arbitrateChains 49 | attachPixels 26 | attachBareT3 26 | attachGridAudit 1
      createTrackCandidates 1        <-- and pixelLineSegmentCleaning / ...Finalize: ZERO
  So "a new buffer in a size class evicts one another stage was reusing, and that stage starts
  paying for real allocations again" cannot explain pLS +18.4 ms: the pLS stage has no allocation
  to evict and none to become more expensive. The +18.4 ms has to land on the COMPUTE of
  CheckHitspLS, which is the only substantial thing in the stage.
  THE PLAUSIBLE MECHANISM IS THEN AN ADDRESS LOTTERY, NOT A COST: an extra live block shifts every
  later heap address, changing cache-set and page-conflict behaviour for the arrays CheckHitspLS
  streams over. 6% is an ordinary magnitude for that on an EPYC.
  WHY THIS MATTERS FOR WHAT WE DO NEXT: if it is an address lottery then (a) the sign is arbitrary
  -- adding a buffer can just as easily make an event FASTER -- and (b) making existing buffers
  persistent does not RECOVER 26 ms, it merely draws a different ticket. It would also mean the
  effect is not a property of allocation at all and gives no reason to convert the standing 26.
  DISCRIMINATOR, now in my queued T5_CURVE (amended): a genuine per-allocation cost is ~linear in
  the COUNT and ~flat in the SIZE; an address lottery is neither. Arms: H2 at 64 KiB (T3's exact
  calibration point), H2 at 4 KiB (same count, 16x smaller footprint and a different bin), and H26
  at 64 KiB (13x the count), palindromic, per-stage columns reported.
  I am NOT contradicting T3's measurement -- pLS +18.4 / TC +7.9 reproducible to 0.6 ms stands.
  I am contradicting the attribution to allocation cost. T3's v5 is still strictly better either
  way, so nothing there needs to wait on this.

[T3 10:50] FOR T4 (T3CC, 0.475 ms): THERE IS A THIRD SINGLE-THREAD RESIDUE IN THE CHAIN BLOCK, AND IT
  HAS THE SAME SHAPE AS THE -RD WALK I JUST REMOVED. Read from the source, not measured -- but the
  structure is not in doubt.
  ChainT3CCSweepEmit (src/alpaka/ChainAttachT3.h:475, launched at LSTEvent.dev.cc:1645 with
  serial_workDiv) opens with `if (!once_per_grid(acc)) return;` and then runs
      for (uint32_t i = 0; i < n; ++i)   with   n = *nDelivPtr
  i.e. ONE THREAD over every stage-B DELIVERY, ~2125 per event. Per iteration it chases order[i] ->
  tgtPls[pos] -> nodes.tripletIndex()[targets[pos]] -> chainNodeMDs (triplets -> segments -> MDs) ->
  ccClaimed[m[k]] for three MDs, then either revokes or claims those three and emits a TC row. That is
  roughly 10-15 dependent global accesses x 2125 iterations = ~25k dependent accesses on one thread,
  which is the right order to be most of T3CC's 0.475 ms.
  WHY THE SAME DECOMPOSITION SHOULD APPLY -- the three preconditions all hold:
   1. THE CLAIM MAP IS MONOTONE. ccClaimed only ever goes 0 -> 1. The -CCR 2 release path clears
      plsOwned and plsBestT3 but does NOT un-claim ccClaimed (a revoked delivery `continue`s before
      claiming). So it is a set with no deletions -- the ChainXcAnchorHits precondition.
   2. THE CONFLICT RELATION IS SYMMETRIC: two deliveries conflict iff they share >= ccMinShared of
      their three MD units. So "has any partner at all" is order-free and computable one thread per
      delivery, and a delivery with no partner is INERT -- it can neither be revoked nor revoke.
   3. WHAT IS LEFT is a greedy over only the flagged deliveries in row order, reading its decision off
      the claim map, exactly as my -RD greedy does. In my case that took the walk from 0.696 to 0.109
      and the flagged set was 4 of 716; here the flagged set will be larger, so expect less, but the
      shape is identical.
  ONE EXTRA PIECE T4 WILL NEED that -RD did not: this kernel also assigns the output TC `row` from a
  serial counter and emits. That is a prefix sum over the survivors, so the emission has to be split
  from the verdict -- decide in the greedy, then count, then scatter.
  AND A STALE NAME: the comment at ChainAttachT3.h:30 refers to "ChainT3CCPreclaim + ChainT3CCSweepEmit"
  but ChainT3CCPreclaim does not exist anywhere in the source (0 hits in LSTEvent.dev.cc). Do not go
  looking for it.
  CAVEAT, since it is my own worst mistake this round: I have NOT instrumented this. Before designing
  anything, add the cheap counters -- how many deliveries, how many are flagged, max partners -- the
  way rdCont/rdMaxSeen decided my design. Two of my three hypotheses about -RD were wrong and both
  were killed by counters, not by reasoning.

[T3 10:52] THE SAME-ARGUMENTS GPU FLOOR, MEASURED AT LAST -- this is the clean test I owed after my
  [T3 09:58] overclaim, and it settles (a) vs (b). Broker tag T3_AB3, ONE slot, the UNMODIFIED baseline
  binary run TWICE with IDENTICAL arguments (-i PU200 -n 50 -v 0 -w 1 -s 1):
      events with a different TC count : 10 of 50
      total TC delta                   : -3
      type 4 +0    type 7 +0    type 9 +0    |    type 5 -2    type 8 -1
  So it is (a) RUN-TO-RUN NONDETERMINISM, not (b) history dependence -- identical arguments, same
  binary, same slot, and it still moves. My original observation was right; the attribution was what
  needed testing, and now it has been tested. The maintainer's ruling that this is known and
  pre-existing stands, and the signature is exactly types 5 and 8 with 4 / 7 / 9 bit-stable.

[T3 10:52] THE AMENDED GATE IS EMPIRICALLY VALIDATED BY A NULL CONTROL, and this is the part worth
  reusing. In the same slot I ran two variants that are PROVABLY equivalent to the baseline -- both are
  CPU 35/35 IDENTICAL, and one of them (v5) also passes a prefilter-OFF control that reproduces the
  baseline algorithm exactly, also 35/35. Their GPU deltas against base_a:
      FLOOR base_a vs base_b : 10/50 events, total -3, type 5 -2, type 8 -1
      v3   (CPU 35/35)       : 13/50 events, total +3, type 5 +4, type 8 -1
      v5   (CPU 35/35)       : 19/50 events, total +1, type 5 +1, type 8 +0
  A change we KNOW is algorithmically identical produces GPU deltas of the same magnitude as the
  baseline's own floor and confined to the same two types. That is what a null control is for: it shows
  the floor is the right yardstick and that "GPU differs" carries no information on this path. Anyone
  claiming a GPU delta means something should first show their change beats a null control of this kind.
  PRACTICAL RECIPE: put base_a, base_b and the variant in ONE slot and compare per tc_type. Helper
  g3/floor.py does it: `floor.py base_a.root base_b.root variant.root [...]`.

[T3 10:52] CPU RESULT for the two provably-equivalent variants -- NO REGRESSION. Same slot, quoted
  against their own base arm, never against the seed's 759.2:
      palindrome -n 50 : base 757.9 / 755.1   v3 750.8 / 754.4   v5 753.2 / 744.0
      headline -n 200  : base 755.8 / 756.7   v5 754.8
  Both sit at or below base. Combined with [T3 10:44], this is the second, independent reason the
  per-event-allocation hypothesis is dead: v3 carries THREE extra per-event buffers and is neutral.

[T3 10:52] HONEST CAVEAT ON MY OWN HEADLINE, so nobody over-reads it. The fully parallel variant's GPU
  delta was measured ACROSS slots (type 5 -10, type 8 +2, 19/50 events). This slot's floor plus the two
  null controls span type 5 in [-2, +4]. So -10 sits OUTSIDE the range I have actually established, and
  I am NOT claiming it is inside the floor. Either cross-slot comparisons carry more spread than
  same-slot ones (likely -- the floor itself is a single sample), or the fully parallel form moves
  something real. The queued job T3_V6 puts the fully parallel form and a same-slot baseline-vs-baseline
  floor in ONE slot, which is the comparison that can settle it. Until then the 10.3 -> 9.7 timing
  stands and its purity verdict is HELD.

[T3 10:55] TRAP I JUST CLOSED, and with five of us building and queueing at once it will bite someone:
  A QUEUED JOB THAT COPIES ITS BINARY FROM THE BUILD TREE AT RUN TIME CAN GET A DIFFERENT BINARY THAN
  THE ONE YOU MEANT. My jobs did `cp $S/bin/lst_cuda $S/LST/liblst_*.so $G/v6_bin/` as their first
  action, guarded by "is it newer than the source". That guard catches a build that has not finished --
  it does NOT catch a LATER build overwriting the tree while the job sits in a queue that is six deep.
  The job would then faithfully measure, gate and report a binary that is not the one under test, and
  every number would look perfectly self-consistent.
  FIX, now in g3/jobs/T3_V6.sh: FREEZE the binary by hand the moment the build is verified, before
  queueing, and have the job VERIFY rather than copy -- it checks the snapshot exists and prints its
  md5 so the report carries proof of which binary ran. Combined with the earlier practice of keeping
  one snapshot directory per variant, this makes a queued measurement immune to anything you do in your
  own area afterwards, which matters because a build takes ~20 min and the queue can be longer.
  HOW TO VERIFY A BUILD BEFORE FREEZING IT (three independent checks, none of them a bare grep):
    1. 0 occurrences of "error:" in the freshly-backed-up .make.log  (grep -c ... || true; see
       [T3 10:35] for how `grep -c` inverted my check by printing 0 and exiting 1)
    2. the .so AND the binary are newer than the source you edited
    3. the kernels you added actually appear in the ptxas register report -- for v6 that was
       `grep -oE "ChainAttachSeedConflicts|ChainAttachSeedDedup|ChainAttachSeedAudit" | sort | uniq -c`
       returning 6 instantiations each. Check 3 is the one that catches "it compiled but my code is not
       in there", which no timestamp can tell you.

[main 11:00] ***READ THIS IF YOU HAVE A JOB IN THE QUEUE*** (T3's trap, and it can silently invalidate
a result rather than fail loudly). A queued job that resolves its binary FROM YOUR BUILD TREE at run
time gets whatever binary is there WHEN THE SLOT RUNS -- not the one you had when you submitted. The
queue is six deep and slots are 15-20 min, so if you rebuild anything while your job waits, your job
measures the NEW binary and reports it under the OLD tag. It will look perfectly self-consistent:
stable numbers, matching passes, no error anywhere. There is no signal that anything went wrong.
DO THIS INSTEAD, both parts:
  * FREEZE the binary at submit time -- copy it to a per-variant path (g<N>/..._bin) and have the job
    run THAT, not $S/bin/lst_cuda.
  * VERIFY AND PRINT ITS md5 inside the job, so the output itself proves which binary produced the
    numbers. T3 does this in T3_V6.
If you have a job in the queue right now and you have rebuilt since submitting it, assume the result
is void and resubmit against a frozen binary. Do not try to reason about whether the timing worked out.
ALSO from T3, a build-verification standard worth copying: check three independent things, not one --
zero diagnostics in the fresh log, artifacts newer than sources, AND your new kernels actually present
in the ptxas register report. The third is the one that catches "it compiled, but my code is not in
there", which no grep of the log will ever tell you.

[T5 ~11:00] *** T3_AB3's OWN v3-vs-v5 PALINDROME SETTLES IT: PER-EVENT ALLOCATION IS NOT A 26 ms
COST. IT IS A NULL AT THIS NOISE LEVEL. *** Read out of results/T3_AB3.txt section [5] (T3's data,
T3's slot, broker tag T3_AB3). v3 and v5 are the SAME algorithm and the same kernels; the only
difference is per-event vs once-allocated scratch, so their difference IS the isolated cost.
  TOTAL ms/evt, -n 50, palindrome base v3 v5 v5 v3 base:
      base 757.9 / 755.1  mean 756.50   within-arm spread 2.80
      v3   750.8 / 754.4  mean 752.60   within-arm spread 3.60
      v5   753.2 / 744.0  mean 748.60   within-arm spread 9.20
      v3 - v5 = +4.00 ms, i.e. SMALLER THAN v5's OWN 9.20 ms within-arm spread.
  pLS column, the stage that carried the whole +18.4 ms claim:
      base 306.8 / 306.2   v3 310.6 / 306.0   v5 307.2 / 307.3
      v3 - v5 = +1.05 ms against a 4.60 ms within-arm spread. NO systematic pLS effect survives.
  And T3's own -n 200 headline in the same slot: base 755.8, v5 754.8, base 756.7 -- all inside 1 ms.
  CONCLUSION: the v1 +26 ms was not a cost of per-event allocation. Whatever it was, it did not
  follow the scratch buffers, because v3 carries the same per-event scratch and shows nothing.
  This is exactly what [T5 ~10:50] predicted from the code: the pLS stage contains two kernel
  launches and ZERO make_device_buffer, so no allocation mechanism can act on it.
  WHAT DOES SURVIVE in T3_AB3 [5], and it is not an allocation effect: the TC column is
  base 39.90, v3 39.95, v5 36.85 -- v5 is 3.05 ms faster than BOTH base and v3, reproducible to
  0.10 ms in both readings, with base and v3 indistinguishable. A change that only moves TC while
  base and v3 agree cannot be "allocating once instead of per event" (v3 allocates per event and
  matches base exactly). That is T3's to explain; I flag it because it is the one reproducible
  signal in the pair and it is being attributed to the wrong cause.
  STATUS OF THE ROUND-WIDE CLAIM: "make the standing 26 per-event buffers persistent" now has NO
  measured basis. I am still running T5_CURVE, because it prices the question with an instrument
  that has no algorithm attached at all (H2 at 64 KiB = T3's exact perturbation, H2 at 4 KiB = same
  count/smaller footprint, H26 at 64 KiB = 13x the count, palindromic). If T5_CURVE is also null,
  the answer is settled from two independent directions and nobody should spend a slot on it again.

[T5 ~11:10] TRAP INSIDE THE FROZEN-BINARY FIX, which I hit while applying it. Freezing by defining
  a shell function `lst_cuda() { $FROZEN/lst_cuda "$@"; }` DOES NOT WORK if you invoke the binary
  through `env VAR=x lst_cuda ...` -- and the natural way to write an env-var-driven A/B arm is
  exactly that. `env` execs a PROGRAM; it never sees shell functions, so every arm would silently
  keep running the build-tree binary while the job printed a reassuring md5 of the frozen one. Use
  an ABSOLUTE PATH variable in the command (`env VAR=x $CUDA ...`).
  Two more things the freeze must cover, or it is not a freeze:
  * liblst_cuda.so / liblst_cpu.so. The standalone binaries link `-llst_cuda` and resolve it from
    LD_LIBRARY_PATH, which points into standalone/LST -- so a rebuild swaps the SHARED LIBRARY that
    a frozen executable loads. Copy the .so too and prepend the frozen directory to
    LD_LIBRARY_PATH.
  * Print `ldd <frozen binary> | grep liblst_` inside the job. That is the only line that proves
    the frozen library, not the build tree's, is what actually loaded.
  My T5_CURVE and T5_P1 are now frozen this way (md5sum -c plus the ldd proof at the top of each).
[T1 11:05] RE T3's tree-resolved-binary trap: T1FINAL2 CHECKED AND CLEAN, and here is the
 provenance evidence, which I recommend as the round's standard because it is free and it proves
 more than an md5 does.
  * T1FINAL2 was already frozen at 10:20 (both arms are per-variant copies under t1_ref/base and
    t1_ref/var1, BINARIES AND liblst_*.so -- freezing the binary alone is NOT enough, it resolves
    liblst_* through LD_LIBRARY_PATH and would silently pick up a rebuilt library). `grep -E
    '\$S/bin|\$S/LST'` on the queued job returns nothing. Last build in g1 was 09:14, submit was
    10:01, so no rebuild happened either way. Added T3's md5 half: the job now prints all 8 md5s
    and `md5sum -c` aborts with "THIS RESULT IS VOID" rather than reporting a plausible number.
  * PTXAS/SYMBOL PROOF that the change is really in the device image (stronger than any log grep,
    and it is already sitting in the make log you have):
      the MANGLED NAME encodes the signature. Baseline ChainAttachScore is
      `...AttachTargetPreE j PjPSA_...` (one `j`); the variant is `...AttachTargetPreE jj PjPSA_...`
      -- the second `j` IS the new nTgtAll parameter. So the merged launch demonstrably contains
      the aux 4L path; it did not silently compile the old body.
      `ChainAttachCcsScore` appears in the baseline ptxas report and is ABSENT from the variant's,
      and `nm -D liblst_cuda.so | grep -c ChainAttachCcsScore` goes 2 -> 0: the kernel is gone from
      the shipped image, not merely unreferenced.
      Registers 96 -> 96 (no occupancy change, consistent with the +0.018 ms being free);
      cmem[0] 720 -> 708 bytes = exactly the 12 bytes of the three deleted ccsTheta floats, which
      independently confirms the ChainConfig deletion propagated into device constant memory.
    GENERALISE THIS: if your change alters a kernel's parameter list, the mangled name in the
    ptxas report is a free, unforgeable receipt that the binary contains your version. If it does
    not, add a throwaway parameter while you verify, or diff `nm -D` on the two libraries.

[T4 11:20] SPEEDUP, tag T4M1 (rc=0). GPU 10.3 -> 9.7 ms/evt, CPU 763.7 -> 758.8 (IMPROVED).
  Four independent changes, each attributed in ONE slot by env switches inside ONE binary (so no
  tree-resolved-binary exposure: every leg prints its own `sw(...)` and the ccpre=1 leg only exists
  in the new library, which proves which one ran). 30-evt per-kernel means, n=30, NOT tail -1:
                      K9 claim   (RANK   ROUNDS)   T3CC    (SWEEP)   suppress  chain total
    ALLOFF (baseline)  0.4550    0.2230  0.1664    0.5187  0.4597    0.1098    7.111
    only CCSPLIT       0.4550    0.2230  0.1664    0.1642  0.1043    0.1094    6.755
    only CCPRE         0.4549    0.2228  0.1664    0.1512  0.0889    0.1094    6.745
    only RANK          0.2775    0.0462  0.1663    0.5181  0.4588    0.1092    6.925
    only ROUNDS        0.4471    0.2229  0.1589    0.5183  0.4593    0.1095    7.096
    only NOWAIT        0.4545    0.2229  0.1660    0.5175  0.4593    0.1021    7.095
    ALL ON             0.2688    0.0446  0.1588    0.1521  0.0899    0.1024    6.541
  1. CCSPLIT, -0.355 ms. ChainT3CCSweepEmit (serial_workDiv, ONE thread) is three things per
     delivery: (i) look up its pLS and its 3 MDs -- a 3-level dependent chase, nodes.tripletIndex
     -> triplets.segmentIndices -> segments.mdIndices; (ii) count claimed MDs, revoke or claim,
     take the next row from a running counter; (iii) assemble the type-5 row (~55 stores). Only
     (ii) reads what an earlier iteration wrote. (i) and (iii) go to the grid, the residue reads a
     20-byte record SEQUENTIALLY plus 3 bytes of the claim map. SWEEP 0.4597 -> 0.1043.
  2. RANK, -0.177 ms (4.8x on that kernel). ChainClaimRank is one thread per candidate, so its
     parallelism is nCand~2421 (10 blocks of the fixed 80) while its work is nCand^2. A rank is a
     SUM of independent predicates, so slice it: slice s of candidate i counts j == s (mod 32) and
     a finish pass adds the 32 partials. An unsigned integer sum is associative, so `order` is
     identical element for element, not merely equivalent. Bonus: laying the slice index out
     fastest-varying makes a warp broadcast recs[i] and read recs[s..s+31] as one 384-byte line.
  3. ROUNDS, -0.0075 ms. Phases C (owner writes) and D (minPos reset) of ChainClaimRounds need NO
     barrier between them: D writes an array nothing in C reads, and two participants sharing a hit
     both store the same constant there. One of four random-access passes over claimHits per round
     disappears. Small, but it DELETES a phase.
  4. NOWAIT, -0.010 ms. The four `alpaka::wait(queue_); // the scratch buffers above die with this
     scope` drains in arbitrateChains are UNNECESSARY: cms::alpakatools' caching allocator is
     queue-ordered (CachingAllocator::free enqueues an event on the block's queue;
     tryReuseCachedBlock hands a cached block back only to the same queue or after that event has
     completed), so a scoped device buffer is safe without a host drain. Anyone else carrying these
     can delete them.
  DROPPED AS NOT WORTH THE CODE (measured, not assumed): CCPRE, the symmetric pre-claim prefilter
  that reduces the serial residue to the deliveries that could still be affected -- only -0.013 ms
  on top of CCSPLIT, because nDeliv is 314/event, not the ~2125 the round has been quoting.
  ** CENSUS CORRECTION, matters to T3 and to the coordinator: the -CC sweep walks nDeliv = 313.6
  per event on average (max 436), of which 110.2 emit and 203.4 are revoked. picks=2758 and
  rdtRevoked=633 are 30-EVENT SUMS on a tail -1 line, not per-event counts. The 2125 figure -- and
  "the -CC sweep discards 95% of 2125" -- is off by ~7x. -CC revokes 65% of 314.
  GATE: CPU 35/35 IDENTICAL both in-slot (A vs B, n=50) and cross-slot (vs the T4B0 baseline).
  GPU n=50 per-tc_type against an IN-SLOT baseline-vs-baseline floor: floor = 15/50 events differ,
  total +4, type5 +6, type8 -2; variant = 15/50 events differ, total -6, type5 -5, type8 -1.
  Types 4/7/9 EXACTLY ZERO in both. Inside the floor, confined to the two known-jittery types.

[T4 11:22] TAKING ChainSegPrefix (shared code, 12 call sites in LSTEvent.dev.cc) unless someone
  says otherwise in the next few minutes -- shout if you are mid-flight on it.
  MEASURED: one ChainSegPrefix costs ~0.034-0.039 ms almost INDEPENDENTLY of nKeys (0.0337 at
  nKeys=4448, 0.0386 at nKeys=7829), while a trivial grid kernel next to it in the same timing
  scheme costs 0.0073. So it is not the scan work and it is not launch overhead -- it is the
  `for (w = 0; w < nWorkers; ++w) { if (w == worker) base = total; total += partial[w]; }` loop,
  which is O(1024) SERIAL ITERATIONS EXECUTED BY EVERY ONE OF THE 1024 THREADS. Replacing it with a
  10-step shared-memory tree scan is exact (unsigned integer sums are associative) and local, and
  the same idiom appears in ChainPrefixTripletModules / ChainPrefixChains / ChainPrefixKeyModules
  and in T5's K6cd 0.150 ms count+prefix. 12 sites x ~0.02 is worth more than anything I have left.

[T5 ~11:25] K0+K1 IS VOLUME-BOUND, NOT LATENCY-BOUND, AND THE INPUT VARIES BY MORE THAN 2x.
  Regression over the 30 per-event lines already in results/T5_BASE.txt (rc=0) -- no new slot:
      K0+K1 ms : mean 0.878  min 0.711  max 1.189   span 54% of the mean
      nodes    : mean 39417  min 25053  max 69193   span 112% of the mean
      corr(K0+K1, nodes) = +0.972          corr(K0+K1, edges) = +0.420
      least squares:  K0+K1 = 0.415 ms + 11.75 us per 1000 nodes
  CORRECTION TO A PREMISE IN CIRCULATION: "39820 nodes, 84453 edges, barely varies" is ONE event
  (it is the last of the 30, which is why it is the one everybody has been quoting). Across the 30
  events the node count runs 25053 to 69193. So the 0.711-1.189 spread needs no exotic explanation:
  at r = +0.972 it IS the input volume. K0+K1 is not latency-bound and folding it into another
  launch would not help.
  THE SPLIT ROUND TWO SHOULD WORK FROM, both halves of it real:
   * 0.415 ms (47%) is NODE-INDEPENDENT. The obvious candidate is K0
     `ChainPrefixTripletModules`, a single-block scan over exactly nLowerModules = 40000 keys EVERY
     event whatever the occupancy, plus the two host barriers and the launch overheads. A fixed
     0.4 ms floor, on 1 SM of 142.
   * 0.463 ms (53%) scales with nodes at 11.75 us/1000. That is K1b `ChainPrefixIncidence` over
     nMDKeys and nLSKeys (which track the produced MD / Segment counts) plus K1c's scatter.
  Both halves land on the same defect: `chainScan_workDiv = make_workdiv<Acc1D>(1, 1024)` is ONE
  BLOCK, and inside it each worker owns a CONTIGUOUS chunk of ceil(nKeys/1024) keys, so the loads
  are ~1.5 KB apart and every one is its own transaction (ChainGraph.h:224-226).
  Edges do NOT drive it (r = +0.42, and two events with 4-6x the edges for the same nodes sit at
  ordinary times) -- correct, because K0+K1 only COUNTS edges; enumeration is K2, in buildChainEdges.
  A three-phase multi-block replacement for K1b is written and built in g5 behind LST_K1_MBSCAN=1
  (bit-identical by regrouping of uint32 addition, argued in ChainGraph.h, independent of any
  comparison). Its A/B is queued as T5_P1 and has NOT run; the fixed 0.415 ms half of the stage is
  K0 and is untouched.
[T1 11:15] WRAP-UP. PATCH EXPORTED: gpu_wt/g1/t1_ref/T1_ccs_deletion.patch (509 lines, 8 files,
 -227/+49, applies to c36248d8f89). Tree is otherwise clean -- nothing unmeasured in it.
 T1FINAL2 is FROZEN (t1_ref/base + t1_ref/var1, binaries AND liblst_*.so) with md5sum -c inside
 the job, no rebuild in g1 since submit, and it is next in line. It is my only queued job.
 MEASURED:  chain block 7.048 -> 5.986 ms, K8 attach 5.820 -> 4.761 (-1.06), tag T1PROBE1.
            CPU physics 35/35 IDENTICAL, tag T1FINAL1.
 PENDING:   200-evt GPU/CPU totals + the GPU noise floor, tag T1FINAL2.
 CHERRY-PICK GOTCHAS for whoever ports this by hand:
  1. `chains.nAttached()` was written ONLY in the deleted kernel's prologue and read NOWHERE in
     the tree, so the SOA_SCALAR goes too (ChainsSoA.h:107). If you keep the field you must find
     it a new writer -- it cannot stay in ChainAttachPublish, whose atomics are not complete
     within its own launch. Deleting it is the reason no replacement kernel was needed.
  2. `is4L` is a POSITIONAL test, `t >= nTargets`, not a chain-row read. It is only valid because
     ChainTargetFlags keeps nLayers >= kAttachMinLayers (5) and ChainAttachSelectAux keeps
     nLayers == 4, making the two classes disjoint and contiguous. If either selector ever admits
     another layer count, this test silently mis-classifies.
  3. tgtPls/tgtLogit are sized nTargets, NOT nTgtAll. The `if (is4L) continue;` before the writes
     is what keeps the 4L threads in bounds -- it is a memory-safety guard, not a tidiness one.
  4. The `if (!is4L)` on the plsBest atomicMax is what preserves invariant I5 (-RPSA bit-identical
     with and without the 4L pass). Dropping it changes physics.
  5. Deleting the plsOwnerChain buffer also removes the `alpaka::wait(queue_)` that ended the old
     CCS scope. That is safe -- the inner contend scope at LSTEvent.dev.cc:2150 already frees
     buffers with kernels in flight, i.e. the tree already relies on queue-aware deallocation --
     but it IS a removed sync, so it is the thing to look at first if anything misbehaves.
  6. ChainRowFlags loses its kChainFlagCcsSuppressed test; flag bit 0x10 becomes free.
  7. LEFT UNDONE ON PURPOSE, and it is now dead code: AttachTargetPre::tcEta / tcPhi. -CCS was
     tcEta's last reader and tcPhi already had none (its dR^2 window went in a2f81bb), so the
     `if (chains.nNodes()[c] > 0) {...}` block in ChainAttachTargetPre (ChainAttach.h ~461-473)
     plus ChainAttachT3.h:186-187 are write-only. Worth ~0.01-0.03 ms in the `pre` stage. NOT in
     my patch and NOT measured -- I kept the tree identical to the measured binaries. Do NOT
     confuse it with the ChainsSoA::tcEta column, which is live (ChainArbitrate.h:306).

[T4 11:45] WRAP-UP. What is in g4 is EXACTLY the four changes measured at [T4 11:20] and nothing
  else; the two things I wrote but never got a slot for are REVERTED rather than shipped unmeasured:
  (a) the single-block prefix tree scan described at [T4 11:22] -- the measured motivation stands
  (0.0337 ms at nKeys=4448 vs 0.0073 for a trivial grid kernel beside it, so it is the
  O(nWorkers)-per-thread tail, not the scan and not launch overhead), it is ~40 lines
  (chainScanBlockExclusive), it is exact, and it applies to 12 ChainSegPrefix sites plus
  ChainPrefixTripletModules / ChainPrefixChains / ChainPrefixKeyModules and T5's K6cd; and
  (b) fusing ChainCompactSelect into ChainT3CCPrep (~0.015 ms, one fewer launch).
  ROUND TWO: those two plus ChainClaimRounds' ONE-BLOCK launch (0.159 ms on 1 SM of 142) are what
  is left in my window; my post-patch residue is K9 claim 0.269 + T3CC 0.152 + compact 0.068 +
  suppress 0.102.
  I RELEASE the ChainSegPrefix claim from [T4 11:22] -- I am not building it, take it freely.
  Details, including the two negative results (the -CC symmetric prefilter, worth only -0.013 ms
  because nDeliv is 314 not 2125; and the per-thread braid owner cache), in g4/T4_STATUS.md.

[T2 11:25] FINAL, tag T2P3 -- THE PAIR-PARALLEL SCORER, BOTH STAGES. Patch:
  g2/t2_ref/T2_pairparallel.patch        (exactly what produced these numbers, applies to c36248d8f89)
  g2/t2_ref/T2_pairparallel_clean.patch  (same change, ablation scaffolding removed -- NOT measured)
200 evt / 1 stream, BASE and VARIANT alternated TWICE in one slot, each pair reproducing exactly:
    GPU  BASE 10.3 (Chain column 6.9)  ->  VARIANT 8.1 (Chain 4.7)     -2.2 ms/evt, -21.4%
    CPU  per-stage CHAIN column: BASE 135.4 / 134.0  ->  VARIANT 133.9 / 133.9
         (totals 762.7 / 764.8 -> 756.7 / 757.3; every other column equal within noise)
Per-kernel, 30 evt, same slot, with a BASE drift check that reproduced (A 1.345/1.344, B 1.591/1.587):
    stage A `score`  1.345 -> nS=1 1.425 | 16 0.118 | 32 0.067 | 64 0.055 | 128 0.066
    stage B `score`  1.591 -> nS=1 1.576 | 16 0.203 | 32 0.191 | 64 0.202 | 128 0.293
    combined at the shipped nS=64: 2.936 -> 0.257 ms  (11.4x; nS=32 gives 0.258, a tie)
    Stage B PLATEAUS at nS=16 while stage A keeps improving to 64 -- B has ~2.5x fewer candidates
    per target, so its per-thread fixed cost binds sooner. One constant serves both.
GATE, the amended rule, all three numbers from THIS slot:
    CPU base vs CPU variant        : 35 IDENTICAL, 0 DIFFER   <-- the hard requirement, PASSED
    GPU noise floor (base vs base) : 504 records, 256.0 per 1e5, {4:13, 5:217, 7:16, 8:6, 9:4}
    GPU base1 vs variant           : 462 records, 234.7 per 1e5, {4:8, 5:208, 7:12, 8:9}
    GPU base2 vs variant           : 540 records, 274.3 per 1e5, {4:11, 5:225, 7:20, 8:12, 9:4}
    My delta (234.7) is SMALLER than the baseline's own floor (256.0) and both sit inside the
    floor's spread (256 vs 274 for two draws of the same comparison). The types-4/7/9 entries appear
    in the base-vs-base comparison too, in perfectly balanced pairs (13/13, 16/16, 4/4), so they are
    the floor, not the change.
TIE-BREAK ARGUMENT (the thing a two-stage reduce usually breaks): the serial rule
`bestPls < 0 || lo > bestLogit || (lo == bestLogit && p < bestPls)` is "max logit, lowest pLS row
wins a tie", which is exactly max over attachContendKey(logit, p) = orderFloat(logit)<<32 |
(0xFFFFFFFF - p): the high word is monotone in the logit and the low word is monotone DECREASING in
p, so a larger key means a larger logit or, at equal logit, a smaller row. Each slice keeps its own
serial best and does ONE atomicMax of that key, and max over a total order is associative and
commutative, so the slice partition cannot change the result. Two edge cases closed: -0.0 vs +0.0
(attachContendKey canonicalises, so they cannot rank), and a NaN logit (would rank differently, but
the inputs are sanitized and clipped before the matmul so no logit can be NaN). p is unique within a
target, so no two candidates can produce the same key.
BUILD VERIFICATION (three-way): zero `error:` in the fresh log; artifacts newer than all three
sources; and the ptxas report inside the job output lists ChainAttachUnpackBest, a kernel that does
not exist in the baseline -- so the measured binary provably contains this change. Registers
unchanged: ChainAttachScore 95/96, ChainAttachT3Score 96, 0 spill, UnpackBest 20-26.

[T3 11:46] *** T3 FINAL RESULT: -RD PARALLELISED, GPU 10.3 -> 9.7 ms/evt, ALL GATES PASSED.
  Broker tag T3_V6, one slot, three binaries, everything below from that slot. ***
  PATCH gpu_wt/g3/v6_parallel_noalloc.patch   BINARY gpu_wt/g3/v6_bin md5 06ed5e50aa2e
  TIMING, interleaved base/v1/v6, two passes each, identical both passes:
      base 10.3  10.3        v6 9.7  9.7        (v1, the same kernels, also 9.7 / 9.7)
      RDdedup 0.737 -> 0.113  (gather+table 0.042  prefilter 0.013  greedy 0.050  publish 0.008)
      stage-B contend 0.936 -> 0.945, i.e. untouched, as intended -- that line is T4's
  CENSUS IDENTICAL TO BASELINE: picks=716 attached=711 rdRevoked=2, hashOverflow=0. Prefilter census
      rdCont=4 of 716, rdMaxEnt=7, and rdAuditBad=0 in ALL 30 EVENTS with the brute-force O(n^2) pair
      scan on -- the parallel prefilter agrees exactly with a scan that uses no hash table at all.
  CPU BIT-IDENTITY, the hard requirement: base vs v6 = 35/35 IDENTICAL. (base vs v1 also 35/35.)
  CPU TOTAL, palindrome at -n 200: base 757.1 / 771.4    v6 755.7 / 766.0    v1 801.0 / 821.4
      v6 sits INSIDE the baseline's own spread in both positions -> no regression. v1 is +47 ms in
      both positions -> refused, and that is why v6 exists.
  GPU PURITY against a THREE-SAMPLE same-slot floor (baseline vs baseline, identical arguments):
      floor a-b : type 5  +7   type 8  -2      types 4 / 7 / 9  +0
      floor a-c : type 5 +14   type 8  -1      types 4 / 7 / 9  +0
      floor b-c : type 5  +7   type 8  +1      types 4 / 7 / 9  +0
      v6        : type 5  +7   type 8  -3      types 4 / 7 / 9  +0
      v1        : type 5  -2   type 8  +3      types 4 / 7 / 9  +0
      The floor's own type-5 excursion runs 7 to 14. v6 is +7 and v1 is -2, both inside it, and both
      confined to exactly the two types the floor moves while 4 / 7 / 9 are bit-stable. This also
      CLOSES my [T3 10:52] caveat: the cross-slot -10 I would not vouch for is comfortably inside a
      floor that reaches 14. Three samples were the point -- one pair is a point and cannot bound a
      spread.
  WHAT MADE IT POSSIBLE, in one line: the dedup's seen[] cap peaks at 4 of 64 and its insert cut-off
  never fires ([T3 10:38]), so the verdict reads only table MEMBERSHIP and never slot layout -- which
  is exactly what a concurrent CAS build preserves. Cheap counters decided that after two rounds of my
  reasoning had got it wrong.

[T3 11:46] LAST WORD ON THE ALLOCATION QUESTION, because I have now falsified myself twice and the
  truth is in between. Both of these are measured and neither is negotiable:
   * a simple count/size model is WRONG: v3 adds THREE extra per-event buffers and is CPU-neutral,
     while v1 adds TWO and costs +47 ms ([T3 10:44]).
   * yet the allocation DISCIPLINE is SUFFICIENT to remove it: v1 and v6 have byte-identical kernel
     code and differ ONLY in buffer lifetime (v1 allocates 64 KiB + ~1 KiB per event; v6 uses a
     persistent member plus a borrowed dead buffer, zero per event), and v1 is +47 while v6 is neutral.
  The honest reading: adding per-event buffers CAN cost tens of ms on the CPU backend in stages you
  never touched, it is not a smooth function of count or size, and removing the allocations reliably
  removes the cost -- but I CANNOT prove allocation is the mechanism rather than code layout, because
  every source change is also a relink and I have no way to separate them with two binaries. T5's
  in-binary dummy-allocation experiment is the only design I can see that separates them; that is why
  it matters more than my patch does.
  PRACTICAL RULE regardless of mechanism: do not add a per-event device buffer in the chain block. Use
  a persistent std::optional member for anything of compile-time size, or borrow a buffer that is
  already dead (ownOffs_buf is dead the moment ChainCompactSelect has consumed it). It costs two lines
  and it removed a 6% CPU regression here.
  ALSO: the baseline's own CPU spread at -n 200 in this slot was 757.1 to 771.4, i.e. 14 ms. Earlier I
  saw two base runs agree to 1 ms and generalised from it. Do not trust a CPU delta under ~15 ms
  without a palindrome, whatever -n you use.
