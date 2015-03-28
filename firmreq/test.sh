#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NDEVICES=4
CLASS_SYSFS=/sys/class/$MODULE
GLOBAL_FIRMWARE_DIR=/lib/firmware

__devname()
{
	echo ${MODULE}$1
}

err=0
cd $(dirname $0)
tmpdir=$(mktemp -d)
if [[ ! -e $GLOBAL_FIRMWARE_DIR ]]; then
	mkdir -p $GLOBAL_FIRMWARE_DIR
	mkdir_GFD=1
fi
fwlnk=$GLOBAL_FIRMWARE_DIR/$MODULE
ln -sfn $tmpdir $fwlnk
for i in $(seq 0 $(( $NDEVICES - 1 ))); do
	device=$(__devname $i)
	echo "$device" > $fwlnk/$device.fw
done
insmod $MODULE.ko ndevices=$NDEVICES
if [[ $? != 0 ]]; then
	echo "$0: failed to load module $MODULE"
	err=1
fi
rm -rf $fwlnk $tmpdir
[[ $err != 0 ]] && exit $err
for i in $(seq 0 $(( $NDEVICES - 1 ))); do
	device=$(__devname $i)
	fwdata=$(cat $CLASS_SYSFS/$device/firmware)
	if [[ "$fwdata" != "$device" ]]; then
		echo -n "$0: unexpected data in firmware for $device. " 1>&2
		echo "expected \"$device\" actual \"$fwdata\"" 1>&2
		err=1
	else
		echo "$0: firmware for $device matches expected data" 1>&2
	fi
done
rmmod $MODULE
[[ "$mkdir_GFD" == "1" ]] && rmdir $GLOBAL_FIRMWARE_DIR
exit $err
