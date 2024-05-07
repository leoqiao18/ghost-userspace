#!/usr/bin/env bash

fname=$1
interval=$2
iterations=$3
base_watts=8.0

sudo pkill "shiv.out"

sudo python3 efs_test/efs_bench.py ${fname}-efs.csv ${interval} ${iterations} ${base_watts}
python3 efs_test/plot_graph.py ${fname}-efs ${interval}
sudo python3 efs_test/cfs_bench.py ${fname}-cfs.csv ${interval} ${iterations} ${base_watts}
python3 efs_test/plot_graph.py ${fname}-cfs ${interval} ${iterations}
