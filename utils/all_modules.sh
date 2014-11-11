#! /bin/bash

UTILS=$(dirname $0)
for m in $(ls); do
	if [[ ! -d $m || "$m" == "$UTILS" ]]; then
		continue
	fi
	echo $m
done
