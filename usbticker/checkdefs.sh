#! /bin/bash

function uphex {
	printf "%X" $1
}

source parsedefs.sh
KERNELVERSION=$(make -s -C $KERNEL kernelversion)
MODALIASFILE=$INSTALL_MOD_PATH/lib/modules/$KERNELVERSION/modules.alias
usbalias="usb:v$(uphex $usbticker_idVendor)p$(uphex $usbticker_idProduct)"
echo "making sure $usbalias does not appear in $MODALIASFILE"
(grep $usbalias $MODALIASFILE > /dev/null) && exit 1
exit 0
