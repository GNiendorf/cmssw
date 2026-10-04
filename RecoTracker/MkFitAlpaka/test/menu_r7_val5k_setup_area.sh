#!/bin/bash
# setup_area.sh <integrated CMSSW area>: freeze the integrated head for val5k-r7 = rsync of the area WITHOUT tmp/ (objects; ~0.5 GB) into
# areas/int/CMSSW + scram b ProjectRename + edmPluginRefresh; areas/{ref,target,tgpu} -> int/CMSSW (ONE area: paired, same binaries for
# every stock module). Checks: MkFitCore carries the NaN guard (D7-a reference), cuda + serial libraries present.
SRC=${1:?usage: setup_area.sh <area CMSSW dir>}; V=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r7_menu/val5k
[ -d $V/areas/int/CMSSW ] && { echo "areas/int exists: remove it by hand first"; exit 1; }
[ $(df --output=avail -BG /mnt/data1 | tail -1 | tr -dc 0-9) -ge 50 ] || { echo "disk < 50 GB free: not copying"; exit 1; }
mkdir -p $V/areas/int && rsync -a --exclude /tmp/ $SRC/ $V/areas/int/CMSSW/ && mkdir -p $V/areas/int/CMSSW/tmp || exit 1
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$V/tmp
cd $V/areas/int/CMSSW/src && scram b ProjectRename > $V/logs/projectrename.log 2>&1 && eval $(scramv1 runtime -sh) && edmPluginRefresh > /dev/null 2>&1
git -C RecoTracker/MkFitCore merge-base --is-ancestor 85a2dba HEAD && echo "MkFitCore NaN guard 85a2dba: present" || echo "WARNING: MkFitCore lacks the NaN guard 85a2dba (D7-a reference)"
for p in RecoTracker/MkFitAlpaka RecoTracker/MkFitAlpakaFormats RecoTracker/MkFitCore RecoTracker/LST; do echo "$p $(git -C $p rev-parse --short HEAD) $(git -C $p status --short | grep -v '^??' | wc -l) modified"; done
ls ../lib/$SCRAM_ARCH | grep -c "MkFitAlpaka.*\(CudaAsync\|SerialSync\)" | sed 's/^/MkFitAlpaka cuda+serial libraries: /'
for a in ref target tgpu; do ln -sfn int/CMSSW $V/areas/$a; done; ls -l $V/areas
