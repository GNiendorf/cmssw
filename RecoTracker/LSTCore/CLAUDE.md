# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LST (Line Segment Tracking) is a high-performance particle track reconstruction algorithm for the CMS detector at the High Luminosity LHC. It uses heterogeneous computing (CPU/CUDA/ROCm) via the Alpaka abstraction layer.

The algorithm reconstructs tracks hierarchically:
- **MiniDoublets (MD)**: Pairs of adjacent hits (2 hits)
- **Segments (LS)**: Pairs of MiniDoublets (2 MDs, 4 hits)
- **Triplets (T3)**: Pairs of Segments sharing a middle MD (3 MDs, 6 hits)
- **Quadruplets (T4)**: Pairs of Triplets sharing a middle Segment (4 MDs, 8 hits)
- **Quintuplets (T5)**: Pairs of Triplets sharing a middle MD (5 MDs, 10 hits)
- **Pixel Line Segments (pLS)**: Inner Tracker (pixel) track seeds
- **Pixel Triplets (pT3)**: pLS matched with a T3 in the Outer Tracker
- **Pixel Quintuplets (pT5)**: pLS matched with a T5 in the Outer Tracker
- **Track Candidates (TC)**: Final output collection

## Standalone Directory

**Important**: All standalone builds and execution must be run from:
```
/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
```

Always `cd` to this directory before running setup, build, or execution commands.

**Note for Claude Code**: Each Bash tool invocation starts a fresh shell at `/mnt/data1/gsn27`. Use `pushd` (NOT `cd` — it gets silently dropped). Prefix ALL commands with:
```bash
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone && source setup.sh && cmsenv && source setup.sh && <your_command>
```

## Build Commands

### CMSSW Build (Primary)
```bash
cd CMSSW_17_0_0_pre2/src
cmsenv
scram b -j 12
```

### Standalone Build
```bash
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
source setup.sh; cmsenv; source setup.sh   # Run ONCE at start of session
lst_make_tracklooper -m            # Clean build (CPU + CUDA by default)
lst_make_tracklooper -C            # CPU only
lst_make_tracklooper -G            # CUDA only
lst_make_tracklooper -R            # ROCm only
lst_make_tracklooper -A            # All backends
lst_make_tracklooper -mcCd         # Clean CPU build with DNN/cut value branches
```

Build flags:
- `-m`: Clean build (make clean first)
- `-c`: Deprecated, but still used
- `-C`: CPU backend only
- `-G`: CUDA backend only
- `-R`: ROCm backend only
- `-d`: Enable cut value / DNN training branches in ntuple output

### Code Checks
```bash
scram b -j 12 code-checks >& c.log
scram b -j 12 code-format >& f.log
```

## Running LST

### Standalone Execution
```bash
lst_cuda -i PU200 -o output.root   # Run CUDA backend
lst_cpu -i PU200 -o output.root    # Run CPU backend

# Options:
#   -i <dataset>   Input dataset (PU200, PU200RelVal, muonGun, etc.)
#   -n <nevents>   Number of events (-1 for all)
#   -v <level>     Verbosity (0=silent, 1=timing, 2=multiplicity)
#   -s <streams>   Concurrent streams
#   -p <ptCut>     Minimum pT cut in GeV (default: 0.8)
#   --allobj       Write all object branches (MD, LS, T3, T4, T5, pLS, pT3, pT5, etc.)
```

### DNN Training Ntuple Generation
For generating ntuples with all branches needed for DNN training:
```bash
# Build with DNN branches enabled
lst_make_tracklooper -mcCd

# Generate training ntuple (use PU200RelVal for more events than PU200)
lst_cpu -i PU200RelVal --allobj -n 300 -p 0.8 -s 32 -v 1 -o training_ntuple.root
```

Key flags for training data:
- `--allobj`: Writes all object branches (t5_*, pLS_*, t3_*, etc.)
- `-s 32`: Use 32 streams for parallel processing
- `-p 0.8`: pT cut at 0.8 GeV

### Performance Analysis
```bash
createPerfNumDenHists -i output.root -o histograms.root
lst_plot_performance.py histograms.root -t "tag"

# Comparison:
lst_plot_performance.py ref.root new.root -L Baseline,New -t "compare" --compare
```

### Timing Benchmarks
```bash
lst_timing PU200                      # Run timing with CUDA backend (default)
lst_timing -b cpu PU200               # Run timing with CPU backend
lst_timing PU200 explicit 200         # Run with specific config, 200 events

# Output: runs multiple stream configurations (1,2,4,6,8 for GPU; 1,4,16,32,64 for CPU)
# and reports average timing per stage
```

### All-in-One Script
```bash
lst_run -f -m -s PU200 -n -1 -t myTag
# -f: compile, -m: clean make, -s: sample, -n: events, -t: tag
```

### CMSSW Workflows
```bash
runTheMatrix.py -w upgrade -n -e -l 24834.703  # CPU workflow
runTheMatrix.py -w upgrade -n -e -l 24834.704  # GPU workflow
makeTrackValidationPlots.py --extended step4_24834.704.root  # MTV plots
```

## Architecture

### Directory Structure
- `interface/` - Public headers and SoA data structures
- `interface/alpaka/` - Device-specific collection types (Host/Device variants)
- `src/` - CPU implementations
- `src/alpaka/` - Alpaka kernels (`*.dev.cc` for device code)
- `standalone/` - Independent build system and analysis tools

### Key Patterns

**Alpaka Namespacing**: All device code uses `ALPAKA_ACCELERATOR_NAMESPACE::lst` for backend abstraction.

**Structure of Arrays (SoA)**: Data structures are defined as SoA for GPU efficiency. Each object type has:
- `*SoA.h` - Layout definition
- `*HostCollection.h` - CPU-side collection
- `*DeviceCollection.h` - GPU-side collection (in `interface/alpaka/`)

**LST Object Types** (defined in `interface/Common.h`):
```cpp
enum LSTObjType : int8_t { T5 = 4, pT3 = 5, pT5 = 7, pLS = 8, T4 = 9 };
```

### Key Files
- `interface/alpaka/LST.h` - Main algorithm interface
- `interface/LSTESData.h` - EventSetup data (geometry, module maps)
- `interface/Common.h` - Constants, types, and parameter structs
- `src/alpaka/LST.cc` - Algorithm orchestration
- `src/alpaka/LSTEvent.dev.cc` - Event processing kernels

### Data Flow
```
LSTInputDeviceCollection → LSTEvent processing → TrackCandidatesDeviceCollection
```

The `LST::run()` method takes input hits and produces track candidates through the hierarchical reconstruction stages.

## Related Package

`RecoTracker/LST` contains CMSSW integration:
- `plugins/alpaka/` - EDProducers for framework integration
- `python/` - Configuration files
- `src/` - EventSetup data producers
