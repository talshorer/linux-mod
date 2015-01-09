#! /bin/sh
FUNCTION="ticker"
idVendor=0x0525
idProduct=0xa4a2
manufacturer="Shorer"
product="Ticker"

GADGET=/sys/kernel/config/usb_gadget/g_$FUNCTION
STRINGSDIR=$GADGET/strings/0x409
FUNCTIONDIR=$GADGET/functions/$FUNCTION.0
CONFIGDIR=$GADGET/configs/c.1
CONFIGSTRINGSDIR=$CONFIGDIR/strings/0x409

mkdir $GADGET
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
