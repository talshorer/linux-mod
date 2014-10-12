#! /bin/bash

MODULE=cpipe
NPIPES=2
DATA="abcdefg"
# BSIZE must be a power of 2
BSIZE=$(echo $DATA | wc -c)
SLEEP_INTERVAL=0.1

err=0
insmod $MODULE.ko npipes=$NPIPES bsize=$BSIZE
for pipe_num in $(seq 0 $(( NPIPES - 1 ))); do
	pipe=/dev/${MODULE}$pipe_num
	for end in 0 1; do
		src=$pipe.$end
		snk=$pipe.$(( 1 - $end ))
		echo "$0: runnind test with source $src and sink $snk" 1>&2
		tmp=$(tempfile)
		head -n 1 $snk > $tmp &
		pid=$!
		sleep $SLEEP_INTERVAL
		if [[ ! -e /proc/$pid ]] ; then
			echo "$0: failed read blocking test with sink $snk" 1>&2
			err=1
		fi
		echo $DATA > $src
		sleep $SLEEP_INTERVAL
		if [[ -e /proc/$pid ]] ; then
			echo "$0: process $pid still running after write to $src" 1>&2
			err=1
		fi
		if [[ "$(cat $tmp)" != "$DATA" ]]; then
			echo "$0: failed readback test with source $src and sink $snk" 1>&2
			err=1
		fi
		rm $tmp
		echo $DATA > $src
		echo $DATA > $src &
		pid=$!
		sleep $SLEEP_INTERVAL
		if [[ ! -e /proc/$pid ]] ; then
			echo "$0: failed write blocking test with source $src" 1>&2
			err=1
		fi
		head -n 1 $snk > /dev/null
		sleep $SLEEP_INTERVAL
		if [[ -e /proc/$pid ]] ; then
			echo "$0: process $pid still running after read from $snk" 1>&2
			err=1
		fi
		head -n 1 $snk > /dev/null
	done
done
rmmod $MODULE
exit $err
