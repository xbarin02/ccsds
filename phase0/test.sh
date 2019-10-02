#!/bin/bash

set -e

make compress

for f in data/*.pgm; do
	echo $f
	./compress $f 2> /dev/null && echo OK
done
