#! /bin/bash

UTILS=$(realpath $(dirname $0))
CHECKPATCH=$KERNEL/scripts/checkpatch.pl
TMPOUTPUT=$(mktemp)
files="$@"
errors=0

if [[ -z "$files" ]]; then files=$($UTILS/all_files.sh); fi

for file in $files; do
	echo $file
	$CHECKPATCH -f $file &> $TMPOUTPUT
	if [[ $? != 0 ]]; then
		let errors+=1
		cat $TMPOUTPUT
	fi
done
echo "errors: $errors"
rm -rf $TMPOUTPUT
