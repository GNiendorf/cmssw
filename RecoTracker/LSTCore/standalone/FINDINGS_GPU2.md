# Shared findings log - GPU TIMING ROUND 2 (agents U1 U2 U3 U4 U5)

Read-write for all five. Append CONFIRMED findings only, with flock:
  flock S/FINDINGS_GPU2.md -c 'echo "[U3 HH:MM] finding: numbers, broker tag, gate result" >> S/FINDINGS_GPU2.md'
A finding is: a measured speedup (with its broker tag AND its gate result), a falsified idea, or a
trap. No progress notes. Round 1's log is FINDINGS_GPU.md -- read it, it is why this round starts
where it does.

--------------------------------------------------------------------------------------------
## THE GOAL

Round 1 took GPU 10.3 -> 5.8 ms/evt (chain block 6.9 -> 2.2). LST master is 4.5.
**THIS ROUND'S GOAL IS PARITY WITH MASTER OR BETTER.**

The arithmetic that matters, from the round-1 exit measurement:
    Hits 0.6 | MD 0.2 | LS 0.2 | T3 0.6 | pLS 0.2     = 1.8 ms  NOT OURS (master pays it too)
    Graph 1.4 | Chain 2.2 | TC 0.3                    = 3.9 ms  OURS
    master's equivalent of our 3.9 is ~2.6 ms.
So we need to find ~1.3 ms inside Graph + Chain + TC. Nothing else counts. Do not spend a slot on
Hits/MD/LS/T3/pLS -- they are shared LST code and are out of scope (see the scope rule below).

--------------------------------------------------------------------------------------------
## HARD SCOPE RULE (maintainer, non-negotiable)

**TOUCH ONLY CHAIN-SPECIFIC CODE.** Round 1 held this and so must you: the nine files it changed
were Chain*.h, ChainConfig.h, ChainsSoA.h, plus LSTEvent.dev.cc/.h where the edits sat strictly
inside attachPixels / attachBareT3 / arbitrateChains, plus the LSTProducer config plumbing. NO
shared LST kernel was touched -- nothing in MiniDoublet, Segment, Triplet, Quintuplet,
PixelTriplet, Kernels.h -- and the pLS stage was left alone entirely.
A change that also speeds up LST master is NOT a deliverable for this project (it moves both sides
of the comparison and closes no gap), and a change to shared code is a maintenance liability we do
not own. If you believe a shared-code change is the only route to something big, LOG IT and stop --
do not build it.

KERNEL COUNT IS A REAL COST. The maintainer's standing position is that this algorithm must end up
SIMPLER than LST and that 86 kernels was already too many. Round 1 added ~5 net. Every kernel you
add must be justified by its own measured milliseconds -- state the count change with your result.
A change that REMOVES a kernel or a barrier is worth more than the same milliseconds bought by
adding one. Deletions are first-class results.

--------------------------------------------------------------------------------------------
## HOW YOU MEASURE (unchanged from round 1, and it is not optional)

Never run a timing command yourself; five agents share one box and a build or a physics run during
a measurement corrupts it, because the number is wall-clock per event.
  bash /mnt/data1/gsn27/here/gpu_wt/broker/submit.sh <TAG> <your-script.sh>
  # poll /mnt/data1/gsn27/here/gpu_wt/broker/results/<TAG>.rc  (exists = finished)
  # read /mnt/data1/gsn27/here/gpu_wt/broker/results/<TAG>.txt
Build through the lock: bash /mnt/data1/gsn27/here/gpu_wt/broker/buildlock.sh lst_make_tracklooper -c
Only broker-produced numbers are findings. Every timing job is an A/B pair in ONE slot.

--------------------------------------------------------------------------------------------
## THE GATE (settled by round 1 the hard way -- do not re-derive it)

1. **CPU bit-identity 35/35 is THE gate.** The CPU backend is deterministic (a repeat gives 35/35
   with zero event-level differences), so this is the only comparison with real discriminating
   power. Non-negotiable for any change meant to be physics-neutral.
2. **A GPU-vs-GPU branch comparison IS NOT A GATE.** The baseline fails against ITSELF at ~250 per
   1e5 TCs over 18/50 events. Round 1 proved this with a null control: two provably-equivalent
   variants moved the output as much as the baseline moved against itself. If you quote a GPU diff
   at all, quote it against a same-slot floor.
3. **A GPU floor needs THREE baseline runs, not two.** One pair is a point and cannot bound a
   spread: the type-5 excursion runs +7 to +14 across three runs.
4. **A CPU TOTAL CANNOT ATTRIBUTE A CHANGE.** Relinking alone moves an untouched stage ~19-25 ms
   REPRODUCIBLY. Attribute via the per-stage column plus a direct per-kernel measurement of your
   own code. Do not trust a CPU delta under ~15 ms without a palindrome (base A B B A base).
5. Do not regress CPU. We are at 758 against master's 763.6 and that lead is an asset.

--------------------------------------------------------------------------------------------
## METHOD LESSONS THAT PRODUCED EVERY ROUND-1 WIN

* **COUNTERS BEAT REASONING.** Two of three hypotheses about one kernel were wrong and cheap
  counters killed both; the retraction of one (a cap assumed to be binding, measured at 4 of 64) is
  what unlocked a 0.6 ms win. Instrument before you design. It costs one cheap slot.
* **CHECK THE LAUNCH SHAPE FIRST, ALWAYS.** It was the answer four times. `ptxas` reports and the
  workdiv need no GPU and no lock, so this is free. A kernel over a ~1e3 object list is ~34 warps on
  a 142-SM L40 -- about 3% of the machine -- and its registers and arithmetic are then irrelevant.
* **PARALLELISM FIRST, ARITHMETIC SECOND.** Ablation showed the MLP matmul was 44% of a scorer's
  cost, but deleting it entirely was worth 0.635 ms while fixing the parallelism was worth 1.29 and
  left the matmul at 0.022.
* **DO NOT ADD A PER-EVENT DEVICE BUFFER.** Persistent member for compile-time sizes, or borrow a
  buffer that is already dead. Two lines of discipline removed a 6% CPU regression. (Whether the
  mechanism is allocation or code layout is STILL UNRESOLVED -- nobody has separated them, because
  every source change is also a relink.)
* **FREEZE YOUR BINARY** -- executable AND liblst_*.so -- and print its md5 inside the job. A queued
  job that resolves from the build tree measures whatever is there when the slot runs, and reports
  it under your tag with no error and no signal.
* **VERIFY A BUILD THREE WAYS**: zero diagnostics in the FRESH log, artifacts newer than sources,
  and your new kernel present in the ptxas report. lst_make_tracklooper prints "successful" even
  when a TU fails, and a grep for "error:" can be fooled by your own tooling's output.
* **PRINT THE WHOLE TIMING BLOCK, NEVER A TAIL.** Round 1's seed map had two holes from `tail -4`
  and they hid two of the five largest lines. The per-event lines are printed PER EVENT -- `tail -1`
  is one event, not an average.

--------------------------------------------------------------------------------------------
## THE MAP (measured on the merged tree at the start of this round)

[appended below once measured -- do not work from round 1's map, four patches have obsoleted it]
