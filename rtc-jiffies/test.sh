#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NCLOCKS=4
KBUILD_MODNAME=$(echo $MODULE | tr '-' '_')
MODULE_SYSFS=/sys/class/$KBUILD_MODNAME
RTC_SYSFS=/sys/class/rtc
# used to protect access to output streams
LOCKFILE=$(mktemp)

__echo()
{
	flock $LOCKFILE -c echo "$@" 1>&2
}

check_diff()
{
	local rtc=$1
	local sysclk=$(date +%s)
	local hwclk=$(cat $RTC_SYSFS/$rtc/since_epoch)
	local clkdiff=$(( $sysclk - $hwclk ))
	[[ $clkdiff -lt 0 ]] && clkdiff=$(( - $clkdiff ))
	# allow a difference of up to one second
	if [[ $clkdiff -gt 1 ]]; then
		__echo "sysclk and $rtc differ by more than 1 seconds "\
				"(hwclk=$hwclk sysclk=$sysclk)"
	fi
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
	for sleeptime in 0 2 4; do
		sleep $sleeptime
		check_diff $rtc || lerr=1
	done
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
rmmod $MODULE
rm $LOCKFILE
exit $err
