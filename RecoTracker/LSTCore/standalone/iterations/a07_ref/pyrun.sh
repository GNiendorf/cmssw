#!/bin/bash
# pyrun.sh <script.py> [args...]  -- runs a python3 script under the standalone env.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
python3 "$@"
