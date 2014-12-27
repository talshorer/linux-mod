#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NPIPES=2
DATA="abcdefg"
# BSIZE must be a power of 2
BSIZE=$(echo $DATA | wc -c)
SLEEP_INTERVAL=1

err=0
cd $(dirname $0)
insmod $MODULE.ko npipes=$NPIPES bsize=$BSIZE
for pipe_num in $(seq 0 $(( NPIPES - 1 ))); do
	pipe=/dev/${MODULE}$pipe_num
	for end in 0 1; do
		src=$pipe.$end
		snk=$pipe.$(( 1 - $end ))
		echo "$0: running test with source $src and sink $snk" 1>&2
		tmp=$(mktemp)
		head -n 1 $snk > $tmp &
		pid=$!
		sleep $SLEEP_INTERVAL
		if [[ ! -e /proc/$pid ]] ; then
			echo -n "$0: failed read blocking test with sink " 1>&2
			echo "$snk" 1>&2
			err=1
		fi
		echo $DATA > $src
		sleep $SLEEP_INTERVAL
		if [[ -e /proc/$pid ]] ; then
			echo -n "$0: process $pid still running after " 1>&2
			echo "write to $src" 1>&2
			err=1
		fi
		if [[ "$(cat $tmp)" != "$DATA" ]]; then
			echo -n "$0: failed readback test with source " 1>&2
			echo "$src and sink $snk" 1>&2
			err=1
		fi
		rm $tmp
		echo $DATA > $src
		echo $DATA > $src &
		pid=$!
		sleep $SLEEP_INTERVAL
		if [[ ! -e /proc/$pid ]] ; then
			echo -n "$0: failed write blocking test with " 1>&2
			echo "source $src" 1>&2
			err=1
		fi
		head -n 1 $snk > /dev/null
		sleep $SLEEP_INTERVAL
		if [[ -e /proc/$pid ]] ; then
			echo -n "$0: process $pid still running after " 1>&2
			echo "read from $snk" 1>&2
			err=1
		fi
		head -n 1 $snk > /dev/null
	done
done
rmmod $MODULE
exit $err
