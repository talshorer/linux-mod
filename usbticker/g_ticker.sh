#! /bin/sh

source parsedefs.sh

FUNCTION=$1
manufacturer="Shorer"
product="Ticker"
idVendor=$usbticker_idVendor
idProduct=$usbticker_idProduct

GADGET=/sys/kernel/config/usb_gadget/g_$FUNCTION
STRINGSDIR=$GADGET/strings/0x409
FUNCTIONDIR=$GADGET/functions/$FUNCTION.0
CONFIGDIR=$GADGET/configs/c.1
CONFIGSTRINGSDIR=$CONFIGDIR/strings/0x409

if ! mkdir $GADGET 2>/dev/null; then
	echo "$0: failed to create gadget" 1>&2
	exit 1
fi
echo $idVendor > $GADGET/idVendor
echo $idProduct > $GADGET/idProduct
mkdir $STRINGSDIR
echo $manufacturer > $STRINGSDIR/manufacturer
echo $product > $STRINGSDIR/product
mkdir $FUNCTIONDIR
mkdir $CONFIGDIR
mkdir $CONFIGSTRINGSDIR
echo $product > $CONFIGSTRINGSDIR/configuration
ln -s $FUNCTIONDIR $CONFIGDIR

echo $GADGET
