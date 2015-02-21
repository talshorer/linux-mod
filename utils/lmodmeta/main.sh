#! /bin/bash

scriptdir=$(realpath $(dirname $0))
lmod=$(realpath $1)

cd $lmod
modules=$($lmod/utils/all_modules.sh)

for m in $modules; do
	path=$lmod/$m
	for f in $(ls $path/*.c); do
		(grep "MODULE_AUTHOR" $f &>/dev/null) || continue
		python $scriptdir/dofile.py $f
	done
done
