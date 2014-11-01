#! /bin/bash

UTILS=$(dirname $0)
for m in $($UTILS/all_modules.sh); do
	ko_file=$m/$m.ko
	if [[ ! -e $ko_file ]]; then
		echo "ERROR: $ko_file not found" 1>&2
		continue
	fi
	echo "$m: $(modinfo $ko_file | sed 's/description: *\(.*\)/\1/; t; d')"
done
