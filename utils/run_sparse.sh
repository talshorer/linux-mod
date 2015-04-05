#! /bin/bash

UTILS=$(realpath $(dirname $0))

modules="$@"

if [[ -z "$modules" ]]; then modules=$($UTILS/all_modules.sh); fi

for m in $modules; do
	echo >&2 $m
	make -C $m clean
	make -C $m all C=2
done 2>&1 1>/dev/null
