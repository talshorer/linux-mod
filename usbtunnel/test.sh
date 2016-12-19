#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
HOST_DRIVER_SYSFS="/sys/bus/usb/drivers/usbtunnel-host"
USBTUNNELCTL="./usbtunnelctl.sh"

sleep_a_bit() { usleep 400000; }

err=0
cd $(dirname $0)
insmod $MODULE.ko
modprobe dummy_hcd num=2
modprobe g_serial
sleep_a_bit
port=$(ls -1 /sys/devices/platform/dummy_hcd.0/usb*/ | grep "^[0-9]\+-[0-9]\+$")
udc="dummy_udc.1"
$USBTUNNELCTL -a $port $udc
orig_driver=$(realpath /sys/bus/usb/devices/$port/driver)
echo -n $port > $orig_driver/unbind
sleep_a_bit
echo -n $port > $HOST_DRIVER_SYSFS/bind
sleep_a_bit
echo -n $port > $HOST_DRIVER_SYSFS/unbind
sleep_a_bit
echo -n $port > $orig_driver/bind
sleep_a_bit
# test logic
err=1
rmmod dummy_hcd
rmmod $MODULE
exit $err
