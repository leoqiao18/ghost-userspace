#!/usr/bin/env bash

cd ./ghost-kernel
cp /boot/config-$(uname -r) .config # Or, make olddefconfig
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str LOCALVERSION "-ghost"
scripts/config --enable CONFIG_DEBUG_INFO_BTF
scripts/config --enable CONFIG_SCHED_CLASS_GHOST  # This is important if we want to use the ghost sched class, to check if it's 'yes' use the --state flag
scripts/config --enable CONFIG_PERF_EVENTS_INTEL_RAPL
yes '' | make localmodconfig
make -j$(nproc)
sudo make headers_install INSTALL_HDR_PATH=/usr                    # to make sures headers are in place for bpf.h, for example.
sudo make modules_install
sudo make install
