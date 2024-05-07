#!/usr/bin/env bash

cd eas_test
make

cd ../efs_test/shiv
make

cd ../process_energy_tracker
make

cd ../sys_base_power
make

cd ../..