#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NBUSES=2
GADGET=${MODULE}_gadget
HOST=${MODULE}_host
GADGET_SYSFS=/sys/class/$GADGET
HOST_SYSFS=/sys/bus/platform/drivers/$HOST
GADGET_DRIVER=g_serial
HOST_DRIVER=cdc_acm

__check_sysfs_link()
{
	sysfsvar()
	{
		eval var=\$${1}_sysfs
		echo $var
	}
	if [[ "$(realpath $(sysfsvar $1)/$2)" != \
			"$(realpath $(sysfsvar $2))" ]]; then
		echo "$0: $2 link in $1's sysfs did not match actual $2" 1>&2
		return 1
	fi
	return 0
}

err=0
cd $(dirname $0)
modprobe udc_core
insmod $MODULE.ko nbuses=$NBUSES
for i in $(seq 0 $(( $NBUSES - 1 ))); do
	bus=${MODULE}$i
	echo "$0: running general test on bus $bus" 1>&2
	gadget_sysfs=$GADGET_SYSFS/${GADGET}$i
	host_sysfs=$HOST_SYSFS/${HOST}.$i
	__check_sysfs_link gadget host || err=1
	__check_sysfs_link host gadget || err=1
done
modprobe $HOST_DRIVER
modprobe $GADGET_DRIVER
sleep 4
echo "$0: running loopback test on bus ${MODULE}0" 1>&2
python test.pyc || err=1
modprobe -r $GADGET_DRIVER
modprobe -r $HOST_DRIVER
rmmod $MODULE
exit $err
