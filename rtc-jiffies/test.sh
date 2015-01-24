#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NCLOCKS=4
KBUILD_MODNAME=$(echo $MODULE | tr '-' '_')
MODULE_SYSFS=/sys/class/$KBUILD_MODNAME
# used to protect access to output streams
LOCKFILE=$(mktemp)

__echo()
{
	flock $LOCKFILE -c echo "$@" 1>&2
}

check_diff()
{
	local rtcdev=$1
	local sysclk=$(date +%c)
	local hwclk=$(hwclock -f $rtcdev -r |
			sed "s/\(.*\)  0\.0\{6\}\ seconds/\1/")
	[[ "$sysclk" == "$hwclk" ]] && return 0
	# TODO allow a difference of up to one second
}

test_one_clock()
{
	local clock=$1
	local lerr=0
	local clock_sysfs=$MODULE_SYSFS/$clock
	local rtc=$(basename $clock_sysfs/rtc*)
	local rtcdev=/dev/$rtc
	__echo "$0: beginning test on clock $clock ($rtc)"
	hwclock -f $rtcdev -w
	# TODO for each value in (0, 2, 4) sleep $value and run check_diff
	__echo "$0: unfinished TODO"
	lerr=1
	__echo "$0: finished test on clock $clock, err=$lerr"
	[[ $lerr != 0 ]] && err=$lerr
}

err=0
cd $(dirname $0)
insmod $MODULE.ko nclocks=$NCLOCKS
__echo "$0: testing $NCLOCKS clocks in parallel"
children=""
for i in $(seq 0 $(( $NCLOCKS - 1 ))); do
	test_one_clock ${KBUILD_MODNAME}$i &
	children="$children $!"
done
wait $children
# debug start
echo "hit return to finish test"
read
# debug end
rmmod $MODULE
rm $LOCKFILE
exit $err
