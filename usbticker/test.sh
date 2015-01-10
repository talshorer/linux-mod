#! /bin/bash

H_MODULE=ticker
G_MODULE=usb_f_ticker
UDC_NAME="dummy_udc"
EXTRA_MODULES="libcomposite dummy_hcd"

err=0
cd $(dirname $0)
for m in $EXTRA_MODULES; do modprobe $m; done
insmod $H_MODULE.ko
insmod $G_MODULE.ko
gadget_configfs=$(sh g_ticker.sh create)
echo $UDC_NAME.0 > $gadget_configfs/UDC
sleep 1
usbdevice_sysfs="/sys/bus/usb/drivers/$H_MODULE/*:*" 2>/dev/null
if [[ -z $usbdevice_sysfs ]]; then
	echo "$0: device not bound to driver" 1>&2
	err=1
fi
rm -rf $gadget_configfs 2> /dev/null
rmmod $G_MODULE
rmmod $H_MODULE
for m in $(for x in $EXTRA_MODULES; do echo $x; done | sed '1!G;h;$!d'); do
	rmmod $m
done
exit $err
