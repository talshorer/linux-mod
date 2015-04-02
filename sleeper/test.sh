#! /bin/bash

DRIVER=$(basename $(dirname $(realpath $0)))
DEBUGFS="/sys/kernel/debug/$DRIVER"
NTHREADS=4
NWAKES=4

get_thread_stat()
{
	thread=$DRIVER$1
	cat $DEBUGFS/stats | grep $thread | sed "s/$thread: \([0-9]\+\)/\1/"
}

wake_thread()
{
	thread=$DRIVER$1
	echo "$0: waking thread $thread" 1>&2
	echo $1 > $DEBUGFS/wake
	return $?
}

err=0
cd $(dirname $0)
insmod $DRIVER.ko nthreads=$NTHREADS
sleep 1
for i in $(seq 0 $(( $NWAKES - 1 ))); do
	for j in $(seq 0 $(( $NTHREADS - 1 ))); do
		if ! wake_thread $j; then
			echo "$0: failed to wake $DRIVER$j" 1>&2
			err=1
		fi
		for k in $(seq 0 $(( $NTHREADS - 1 ))); do
			if [[ $k -gt $j ]]; then
				expected_stat=$i
			else
				expected_stat=$(( $i + 1 ))
			fi
			actual_stat=$(get_thread_stat $k)
			if [[ $actual_stat != $expected_stat ]]; then
				echo -n "$0: stat on thread $DRIVER$k " 1>&2
				echo -n "was unexpected. expected " 1>&2
				echo "$expected_stat, actual $actual_stat" 1>&2
			err=1
			fi
		done
	done
done
rmmod $DRIVER
exit $err
