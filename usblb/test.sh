#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NBUSES=4
GADGET_CLASS=${MODULE}_gadget
HOST_CLASS=${MODULE}_host
GADGET_SYSFS=/sys/class/$GADGET_CLASS
HOST_SYSFS=/sys/class/$HOST_CLASS
GADGET_DRIVER=g_zero

function __check_sysfs_link {
	function sysfsvar {
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
for ((i=0; i<$NBUSES; i++)); do
	bus=${MODULE}$i
	echo "$0: running test on bus $bus" 1>&2
	gadget_sysfs=$GADGET_SYSFS/${GADGET_CLASS}$i
	host_sysfs=$HOST_SYSFS/${HOST_CLASS}$i
	__check_sysfs_link gadget host || err=1
	__check_sysfs_link host gadget || err=1
done
modprobe $GADGET_DRIVER
sleep 1
modprobe -r $GADGET_DRIVER
###############################################################################
############################## debug hacks start ##############################
figlet $MODULE shell
if [[ $err == 0 ]]; then bash; fi
############################### debug hacks end ###############################
###############################################################################
rmmod $MODULE
exit $err
