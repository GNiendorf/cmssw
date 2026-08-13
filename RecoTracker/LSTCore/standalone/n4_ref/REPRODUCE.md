# N4 -- reproduction

Baseline HEAD `a5cebaaaafc`. Two candidates.

## A. `T4A4` -- attach-eligibility / the pT4 product. NO BUILD.
The knob is already in the shipped tree (`interface/ChainConfig.h::chainConfigT4Env`).
Ship binary `standalone/bin/lst_cpu` md5 `4da4e362d2867427d82726b329dbfdb6`,
lib `64c208c2beb32407e64145dd2d0f1c4d`.

    bash n4_ref/gates.sh BASE  all ""
    bash n4_ref/gates.sh T4A4  all "LST_T4_ATTACH_MIN=4"
    python3 p4_ref/jetgate.py --split tune n4_ref/runs/BASE_jet.root n4_ref/runs/T4A4_jet.root --labels SHIP,T4A4
    python3 p4_ref/paired_rle.py n4_ref/runs/BASE_pu.root     n4_ref/runs/T4A4_pu.root     SHIP T4A4
    python3 p4_ref/paired_rle.py n4_ref/runs/BASE_cube50.root n4_ref/runs/T4A4_cube50.root SHIP T4A4
    python3 p4_ref/paired_rle.py n4_ref/runs/BASE_cubehi5.root n4_ref/runs/T4A4_cubehi5.root SHIP T4A4

TO SHIP: `interface/ChainConfig.h`, `static constexpr int kAttachMinLayers = 5;` -> `4`.
(One constant. The env knob then becomes redundant and can be dropped.)

## B. `T4F100` -- the density-conditioned withdrawal of the E1-B2 far-dca free pass. NEEDS THE PATCH.
Patch `n4_ref/t4_fardens.patch` md5 `1928b66f3aea47f231a19ed3f2f791a1`, 127 lines, 2 files
(`interface/ChainConfig.h`, `src/alpaka/ChainGate.h`). Built in `gpu_wt/g5` at `a5cebaaaafc`:

    cd gpu_wt/g5/src && git reset --hard && git checkout -f --detach a5cebaaaafc
    git apply /path/to/n4_ref/t4_fardens.patch
    cd RecoTracker/LSTCore/standalone && source setup.sh && cmsenv && source setup.sh
    lst_make_tracklooper -mcC          # 0 `error:` in .make.log.1786633318
    # bin/lst_cpu md5 c3845715ee491f69571782d029446f1a
    # LST/liblst_cpu.so md5 dfe9a4fb1d4d066c42aa9960e053bf7e   (snapshot: n4_ref/snap/)

    bash n4_ref/gates2.sh N4NUL  all ""                              # the inertness control
    bash n4_ref/gates2.sh T4F100 all "LST_T4_FARDENS_RHO0=100"
    bash n4_ref/gates2.sh UN4    all "LST_T4_FARDENS_RHO0=100 LST_T4_ATTACH_MIN=4"   # the UNION
    python3 t4_ref/bitid.py n4_ref/runs/BASE_jet.root n4_ref/runs/N4NUL_jet.root     # inertness

TO SHIP: `interface/ChainConfig.h`, `float t4FarDensRho0 = 0.f;` -> `100.f`.
The other new knob in the patch (`t4ExDensDelta`) is measurement scaffolding for the CLOSED
`T4X20` arm and can be stripped; the shippable part is one float and two lines in `ChainGate.h`.

## Sample discipline
Jets TUNE = rows 0-499 of `jet_ref/trackingNtuple_jets_1000.root` (`-n 500 -s 16 -J`);
500-999 never opened. PU200 tune = `event_1000.root` NAMED BY PATH -- `-i PU200RelVal` is the
DIRECTORY and reads the sealed `event_2000..7000`.
Cubes: `cube50` full, `cube50_highPt` first 5000 entries, both `-s 4`.

## Offline instruments (no run, JPR3's 450-event corpus `jpr3_ref/fun`)
    python3 n4_ref/t4price.py      # -> n4_ref/t4tab.npz, the separability tables
    python3 n4_ref/t4branch.py     # the branch decomposition
    python3 n4_ref/nn4.py          # -> n4_ref/nn4.npz, the dRnn tables for N2
    bash    n4_ref/dens.sh <jet|pu|cube50|cubehi> <first> <last> <slot>   # chain-dump census
    python3 n4_ref/densfar.py      # the far-cell conditioning table by sample
