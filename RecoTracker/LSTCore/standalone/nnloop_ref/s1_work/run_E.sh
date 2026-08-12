#!/bin/bash
# S1: as soon as arm E (tiered displaced weighting) finishes -- fit its stratified table, export the
# header, rebuild g1, and run the decisive gates (PU200 tune + both cubes + event_2000 holdout).
W=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/s1_work
G=/mnt/data1/gsn27/here/gpu_wt/g1
until [ -f $W/models/edge_E_tier816.pt ]; do sleep 15; done
cd $W
CUDA_VISIBLE_DEVICES=0 python3 wp_table.py --model models/edge_E_tier816.pt --out wp_E_strat.json --strat > logs/wp_E.log 2>&1
CUDA_VISIBLE_DEVICES=0 python3 weldrank.py 40 models/edge_C_cos3e3.pt:wp_C_strat.json models/edge_D_dw.pt:wp_D_strat.json models/edge_E_tier816.pt:wp_E_strat.json > logs/weldrank_all.log 2>&1
python3 export_edge.py --model models/edge_E_tier816.pt --table wp_E_strat.json --out edge_hdr_S1E.h > logs/export_E.log 2>&1
cp edge_hdr_S1E.h $G/src/RecoTracker/LSTCore/src/alpaka/EdgeNetworkWeights.h
cd $G && bash build.sh -mC > s1_build_E.log 2>&1
L=$(ls -t $G/src/RecoTracker/LSTCore/standalone/.make.log.* | head -1)
echo "E BUILD errors: $(grep -c 'error:' $L)  ($L)"
R=$G/s1_run.sh
V=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3
bash $R S1E_tune   -i PU200RelVal        -n 1000 -s 8 -p 0.8 &
bash $R S1E_h2000  -i $V/event_2000.root -n 1000 -s 8 -p 0.8 &
bash $R S1E_cube50 -i cube50             -n 5000 -s 4 -p 0.8 &
bash $R S1E_cubehi -i cube50_highPt      -n 5000 -s 4 -p 0.8 &
wait
echo "S1E GATES DONE"
