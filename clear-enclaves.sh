#!/usr/bin/env bash

for i in /sys/fs/ghost/enclave_*/ctl; do echo destroy > $i; done
