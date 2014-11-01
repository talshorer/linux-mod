#! /bin/bash

DRIVER=$(basename $(dirname $(realpath $0)))
DEVTEMPLATE=/dev/$DRIVER
DEV=${DEVTEMPLATE}a
MOUNTT=vfat
MOUNTP=mountp
DATA="hello world!"
FILE="hi.txt"

err=0
cd $(dirname $0)
insmod $DRIVER.ko ndevices=2 nsectors=1024 hardsect_size=1024
mkfs -t $MOUNTT $DEV
mkdir -p $MOUNTP
mount -t $MOUNTT $DEV $MOUNTP
echo $DATA > $MOUNTP/$FILE
umount $MOUNTP
mount -t $MOUNTT $DEV $MOUNTP
readback=$(cat $MOUNTP/$FILE)
if [[ "$readback" != "$DATA" ]]; then
	echo "$0: failed file readback check" 1>&2
	err=1
else
	echo "$0: passed file readback check" 1>&2
fi
if [[ ! -e ${DEVTEMPLATE}b ]]; then
	echo "$0: device b not created" 1>&2
	err=1
else
	echo "$0: device b properly created" 1>&2
fi
if [[ -e ${DEVTEMPLATE}c ]]; then
	echo "$0: device c created" 1>&2
	err=1
else
	echo "$0: device c properly not created" 1>&2
fi
echo "$0: sleeping to allow all IO operations on $MOUNTP to complete"
sleep 1
umount $MOUNTP
rmmod $DRIVER
exit $err
