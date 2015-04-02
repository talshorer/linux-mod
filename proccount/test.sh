#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
PROCFILE=/proc/opencounter
NITERS=0x100

err=0
cd $(dirname $0)
insmod $MODULE.ko
if [[ ! -e $PROCFILE ]]; then
	echo "$0: $PROCFILE doesn't exist after loading $MODULE"
	err=1
fi
for i in $(seq 0 $(( $NITERS - 1 ))); do
	value=$(cat $PROCFILE)
	if test $value != $i; then
		err=1
		echo -n "$0: read value of $PROCFILE unexpected. " 1>&2
		echo "expected $i, actual $value" 1>&2
		break
	fi
done
rmmod $MODULE
if test $err == 0; then
	echo -n "$0: successfully read expected values from $PROCFILE " 1>&2
	echo "$NITERS times" 1>&2
fi
exit $err
