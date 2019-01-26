#!/bin/bash

set -e -u

# sed "s/^\s*#\s*define\s*CONFIG_PERFTEST_TYPE\s*[0-9]\+\s*$/#define CONFIG_PERFTEST_TYPE 1/" config.h
function config
{
	sed "s/^\s*#\s*define\s*$1\s*[0-9]\+\s*$/#define $1 $2/" -i config.h
}

declare -A CONFIG

CONFIG[4:3-interleaved-blocks]="0 2 2 2 16 64"
CONFIG[4:3-interleaved-strips]="0 2 2 1 16 64"
CONFIG[4:3-sequential-sl-quad]="0 2 2 0 16 64"
CONFIG[4:3-sequential-sl-lines]="0 2 1 0 16 64"
CONFIG[4:3-sequential-separable-sl]="0 2 0 0 16 64"
CONFIG[4:3-sequential-separable-ml]="0 1 0 0 16 64"
CONFIG[4:3-sequential-separable-conv]="0 0 0 0 16 64"

CONFIG[16:9-interleaved-blocks]="2 2 2 2 16 64"
CONFIG[16:9-interleaved-strips]="2 2 2 1 16 64"
CONFIG[16:9-sequential-sl-quad]="2 2 2 0 16 64"
CONFIG[16:9-sequential-sl-lines]="2 2 1 0 16 64"
CONFIG[16:9-sequential-separable-sl]="2 2 0 0 16 64"
CONFIG[16:9-sequential-separable-ml]="2 1 0 0 16 64"
CONFIG[16:9-sequential-separable-conv]="2 0 0 0 16 64"

CONFIG[stripmap-interleaved-blocks]="1 2 2 2 16 128"
CONFIG[stripmap-interleaved-strips]="1 2 2 1 16 128"
CONFIG[stripmap-sequential-sl-quad]="1 2 2 0 16 128"
CONFIG[stripmap-sequential-sl-lines]="1 2 1 0 16 128"
CONFIG[stripmap-sequential-separable-sl]="1 2 0 0 16 128"
CONFIG[stripmap-sequential-separable-ml]="1 1 0 0 16 128"
CONFIG[stripmap-sequential-separable-conv]="1 0 0 0 16 128"

for i in "${!CONFIG[@]}"; do
	echo "measurement: $i"

	ARG=(${CONFIG[$i]})

	config CONFIG_PERFTEST_TYPE ${ARG[0]}
	config CONFIG_DWT1_MODE ${ARG[1]}
	config CONFIG_DWT2_MODE ${ARG[2]}
	config CONFIG_DWT_MS_MODE ${ARG[3]}
	config CONFIG_PERFTEST_NUM ${ARG[4]}

	make clean
	make perftest EXTRA_CFLAGS=-fprofile-generate EXTRA_LDLIBS=-lgcov

	./perftest

	config CONFIG_PERFTEST_NUM ${ARG[5]}
	make clean
	make perftest EXTRA_CFLAGS=-fprofile-use

	./perftest | tee plot-$i.txt
done
