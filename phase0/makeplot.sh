#!/bin/sh

# sed "s/^\s*#\s*define\s*CONFIG_PERFTEST_TYPE\s*[0-9]\+\s*$/#define CONFIG_PERFTEST_TYPE 1/" config.h
function config
{
	sed "s/^\s*#\s*define\s*$1\s*[0-9]\+\s*$/#define $1 $2/" -i config.h
}

# 4:3
config CONFIG_PERFTEST_TYPE 0
config CONFIG_PERFTEST_NUM 16

make clean
make EXTRA_CFLAGS=-fprofile-generate EXTRA_LDLIBS=-lgcov

./perftest

config CONFIG_PERFTEST_NUM 64
make clean
make EXTRA_CFLAGS=-fprofile-use

./perftest
