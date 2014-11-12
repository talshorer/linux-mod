#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
DRIVERS="a b c d e f"
NDEVICES=8
BUS_SYSFS=buslink
ADD=add
DEL=del

function driver_sysfs {
	echo $BUS_SYSFS/drivers/$1
}

function device_sysfs {
	echo $BUS_SYSFS/devices/$1
}

err=0
cd $(dirname $0)
insmod $MODULE.ko
for driver in $DRIVERS; do
	echo "$0: running test with driver $driver and $NDEVICES devices" 1>&2
	echo $driver > $BUS_SYSFS/$ADD
	DRIVER_SYSFS=$(driver_sysfs $driver)
	if [[ ! -e $DRIVER_SYSFS ]]; then
		echo "$0: failed to create driver $driver" 1>&2
		err=1
	fi
	for devid in $(seq 0 $(( $NDEVICES - 1 ))); do
		echo $devid > $DRIVER_SYSFS/$ADD
		device=$driver.$devid
		DEVICE_SYSFS=$(device_sysfs $device)
		if [[ ! -e $DEVICE_SYSFS ]]; then
			echo "$0: failed to create device $device" 1>&2
			err=1
		fi
		echo $devid > $DRIVER_SYSFS/$DEL
		if [[ -e $DEVICE_SYSFS ]]; then
			echo "$0: failed to remove device $device" 1>&2
			err=1
		fi
	done
	devid=0
	echo $devid > $DRIVER_SYSFS/$ADD
	echo $driver > $BUS_SYSFS/$DEL
	if [[ -e $DRIVER_SYSFS ]]; then
		echo "$0: failed to remove driver $driver" 1>&2
		err=1
	fi
	device=$driver.$devid
	if [[ -e $(device_sysfs $device) ]]; then
		echo -n "$0: device $device exists after " 1>&2
		echo "removal of driver $driver" 1>&2
		err=1
	fi
done
driver="hello"
echo "$0: checking driver $driver is deleted upon removal of the module" 1>&2
echo $driver > $BUS_SYSFS/$ADD
rmmod $MODULE
if [[ -e $(driver_sysfs $driver) ]]; then
		echo -n "$0: driver $driver exists after " 1>&2
		echo "removal of the module" 1>&2
		err=1
	fi
exit $err
