#! /bin/bash

DRIVER=sleeper
DEBUGFS=debugfs
NTHREADS=4
NWAKES=4

get_thread_stat()
{
	thread=$DRIVER$1
	cat $DEBUGFS/stats | grep $thread  | sed "s/$thread: \([0-9]\+\)/\1/"
}

wake_thread()
{
	thread=$DRIVER$1
	echo "$0: waking thread $thread" 1>&2
	echo $1 > $DEBUGFS/wake
}

err=0
insmod $DRIVER.ko nthreads=$NTHREADS
for i in $(seq 0 $(( $NWAKES - 1 ))); do
	for j in $(seq 0 $(( $NTHREADS - 1 ))); do
		wake_thread $j
		for k in $(seq 0 $(( $NTHREADS - 1 ))); do
			if [[ $k > $j ]]; then
				expected_stat=$i
			else
				expected_stat=$(( $i + 1 ))
			fi
			actual_stat=$(get_thread_stat $k)
			if [[ $actual_stat != $expected_stat ]]; then
				echo -n "$0: stat on thread $DRIVER$k was unexpected. " 1>&2
				echo "expected $expected_stat, actual $actual_stat" 1>&2
			err=1
			fi
		done
	done
done
rmmod $DRIVER
exit $err
