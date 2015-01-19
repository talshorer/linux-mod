#! /bin/bash

FUNCTION=ticker
H_MODULE=$FUNCTION
G_MODULE=usb_f_$FUNCTION
UDC_NAME="dummy_udc"
EXTRA_MODULES="libcomposite dummy_hcd"
INTERVAL=1000
NTICKS=4

unload_modules()
{
	rmmod $G_MODULE
	rmmod $H_MODULE
	for m in $(for x in $EXTRA_MODULES; do echo $x; done | \
			sed '1!G;h;$!d'); do
		rmmod $m
	done
}

err=0
cd $(dirname $0)
for m in $EXTRA_MODULES; do modprobe $m; done
insmod $H_MODULE.ko
insmod $G_MODULE.ko
gadget_configfs=$(sh g_ticker.sh $FUNCTION)
err=$?
if [[ $err != 0 ]]; then
	unload_modules
	exit $err
fi
echo $UDC_NAME.0 > $gadget_configfs/UDC
echo $INTERVAL > $gadget_configfs/functions/$FUNCTION.0/interval
sleep 2
usbdevice_sysfs="/sys/bus/usb/drivers/$H_MODULE/*:*" 2>/dev/null
read_ticks()
{
	cat $usbdevice_sysfs/ticks
}
if [[ -z $usbdevice_sysfs ]]; then
	echo "$0: device not bound to driver" 1>&2
	err=1
fi
echo "$0: running test for $NTICKS ticks" 1>&2
initial=$(read_ticks)
for i in $(seq 0 $(( $NTICKS - 1 ))); do
	expected=$(( $initial + $i ))
	ticks=$(read_ticks)
	if [[ $ticks != $expected ]]; then
		echo -n "$0: unexpected ticks. " 1>&2
		echo "expected $expected actual $ticks" 1>&2
		err=1
	fi
	sleep 1
done
rm -rf $gadget_configfs 2> /dev/null
unload_modules
exit $err
