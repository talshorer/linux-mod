#! /bin/bash

UTILS=$(realpath $(dirname $0))
CHECKPATCH=$KERNEL/scripts/checkpatch.pl
TMPOUTPUT=$(mktemp)
TMPPATCH=$(mktemp)
files="$@"
errors=0

function __run_checkpatch {
	local file=$1
	rm -f $TMPPATCH
	echo "Signed-off-by: Dummy" >> $TMPPATCH
	diff -urN /dev/null $file >> $TMPPATCH
	$CHECKPATCH $TMPPATCH
	return $?
}

if [[ -z "$files" ]]; then files=$($UTILS/all_files.sh); fi

for file in $files; do
	echo $file
	__run_checkpatch $file &> $TMPOUTPUT
	if [[ $? != 0 ]]; then
		let errors+=1
		cat $TMPOUTPUT
	fi
done
echo "errors: $errors"
rm -rf $TMPOUTPUT $TMPPATCH
