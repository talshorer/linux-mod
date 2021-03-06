#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
DEVICE_MODULE=$MODULE-device
NDEVICES=4
PLATFORM_SYSFS=/sys/bus/platform

err=0
cd $(dirname $0)
insmod $MODULE.ko
insmod $DEVICE_MODULE.ko ndevices=$NDEVICES
driver=$MODULE
driver_sysfs=$PLATFORM_SYSFS/drivers/$driver
for i in $(seq 0 $(( $NDEVICES - 1 ))); do
	device=$MODULE.$i
	echo "$0: running test with device $device" 1>&2
	device_sysfs=$PLATFORM_SYSFS/devices/$device
	if [[ ! -e $device_sysfs ]]; then
		echo "$0: device $device doesn't exist" 1>&2
		err=1
		continue
	fi
	link_target=$(realpath $device_sysfs/driver)
	if [[ "$link_target" != "$driver_sysfs" ]]; then
		echo "$0: device $device not bound to driver $driver" 1>&2
		err=1
	fi
done
rmmod $DEVICE_MODULE
rmmod $MODULE
exit $err
