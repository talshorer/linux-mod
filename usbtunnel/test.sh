#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
HOST_DRIVER_SYSFS="/sys/bus/usb/drivers/${MODULE}_host"
HOST_DRIVER_MATCH="$HOST_DRIVER_SYSFS/match"

err=0
cd $(dirname $0)
insmod $MODULE.ko
modprobe dummy_hcd num=2
port=$(ls -1 /sys/devices/platform/dummy_hcd.0/usb*/ | grep "^[0-9]\+-[0-9]\+$")
udc="dummy_udc.1"
echo "add $port $udc" > $HOST_DRIVER_MATCH
# test logic
err=1
rmmod dummy_hcd
rmmod $MODULE
exit $err
