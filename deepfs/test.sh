#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
MOUNTP=mp

exittest()
{
	rmmod $MODULE
	exit $err
}

err=0
cd $(dirname $0)
insmod $MODULE.ko
mkdir -p $MOUNTP
mount -t $MODULE none $MOUNTP || err=1
if [[ $err == 1 ]]; then
	echo "$0: failed to mount" 1>&2
	exittest
fi
echo "$0: mount successful" 1>&2
umount $MOUNTP
sleep 1
exittest
