#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NCLOCKS=4
# used to protect access to output streams
LOCKFILE=$(mktemp)

__echo()
{
	flock $LOCKFILE -c echo "$@" 1>&2
}

test_one_clock()
{
	local clock=$1
	local lerr=0
	__echo "beginning test on clock $clock"
	# test logic
	__echo "finished test on clock $clock, err=$lerr"
	[[ $lerr != 0 ]] && err=$lerr
}

err=0
cd $(dirname $0)
insmod $MODULE.ko nclocks=$NCLOCKS
__echo "testing $NCLOCKS clocks in parallel"
children=""
for i in $(seq 0 $(( $NCLOCKS - 1 ))); do
	test_one_clock $i &
	children="$children $!"
done
wait $children
rmmod $MODULE
rm $LOCKFILE
exit $err
