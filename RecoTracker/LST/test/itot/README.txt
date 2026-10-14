OT extension off + LST/mkFit changes: research branch (IT/OT study, rounds 2-3). Not PR code.

WHAT THIS BRANCH IS
- Base: #290 (aea1b7b7bd2, mkfit-alpaka-heterogeneous).
- Commit 1 ("Cfinal"):
  - the Patatrack CA without its OT extension: the cluster-size doublet window and a barrel 3-hit path (the CA code); the 43 pairs, caps and q5 are set in s3_block.py / arms_block.py;
  - Torch pass3hit;
  - LST otpull: unused OT barrel T3s pull the CA's 2-pixel-layer cells, joint fit chi2 < 1, on GPU;
  - the pT5 arbitration score and the inward rPhi cut at all pT (with the A5 working point);
  - noT5cc + c2, and the retrained Torch model file.
- Commit 2 ("Cfinal2"):
  - mkFit duplicate removal by survivors only;
  - the otpull pixel stub for pixel-less T5s;
  - the pT5 outward rPhi cut off above 5 GeV (inward x25);
  - otpull memory trims;
  - global-atomics scope fixes (alpaka::hierarchy::Blocks).
- The code is identical, file by file, to the area that was measured.

HOW TO RUN (CPU or GPU full HLT menu, D121)
- source env_Cfinal2.sh in the shell that runs cmsRun, or the patatrack benchmark driver. LST reads the pT5 knobs from the environment, and the configs refuse to load without them.
- cmsRun full_Cfinal2_cpu.py   (or full_Cfinal2_gpu.py)
- The extension-off base without the round-2/3 changes: full_S2_{cpu,gpu}.py, with no env knobs. Its LST still runs this branch's code (noT5cc + c2 are compiled in), which is what the study calls S2.
- final_{cpu,gpu}.py are #290's menus, made by cmsDriver.py with the command in their header, and read local RAW files. Regenerate them, or edit the input, for other samples.
- ITOT_DIR overrides the directory the configs exec their blocks from (by default the directory of the config).

RESULTS SO FAR (MTV hltGeneralTracks, D121 PU200, points, ttbar / QCD)
Dev set, 332 ttbar + 500 QCD events, Cfinal2 vs #290:
- central -0.04 / +0.74;
- vxy 2.5-10 cm -0.13 / +0.05; vxy > 10 cm -0.05 / +1.15;
- QCD pT > 100 +1.07; QCD dense core +0.82;
- fake -0.048 / -0.006; duplicate +0.34 / +0.31 (the noT5cc choice).
- Real statistical errors are about 3x sqrt(dn)/N.
- The holdout and the timing of Cfinal2 are running.

Cost of Cfinal (commit 1) vs #290, 4 jobs x 16 threads, full menu:
- CPU -19.9% / -18.0% box ms;
- GPU peak memory -25.6% / -27.6%; 16-job deck -22.6%.

KNOWN ISSUES BEFORE ANY PR
- The env knobs (LST_PT5_*) should become configuration parameters.
- The diagnostic hooks (LST_OTPULL_T5DUMP / T5ROT / KEYCHECK, LST_OTPULL_PRINT / TIME, CA_OTPULL_TIME) should be removed. T5ROT changes physics if it is set.
- The Torch model .pt belongs in cms-data, not in cmssw.
