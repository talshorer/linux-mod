#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
MOUNTP=mp
MAXDEPTH=4

exittest()
{
	rmmod $MODULE
	exit $err
}

check_subdirs()
{
	depth=$1
	ls &> /dev/null
	for i in $(seq 0 $(( $MAXDEPTH - $depth - 1 ))); do
		subdir=sub0x$(printf %02x $i)
		if [[ -e $subdir ]]; then
			cd $subdir
			check_subdirs $(( $depth + 1 ))
			cd ..
		else
			echo "$0: missing subdir $(pwd)/$subdir" 1>&2
			err=1
		fi
	done
}

err=0
cd $(dirname $0)
insmod $MODULE.ko
mkdir -p $MOUNTP
mount -t $MODULE none $MOUNTP -o max_depth=$MAXDEPTH || err=1
if [[ $err == 1 ]]; then
	echo "$0: failed to mount" 1>&2
	exittest
fi
echo "$0: mount successful" 1>&2
echo "$0: running recursive files check" 1>&2
cd $MOUNTP
check_subdirs 0
cd ../
umount $MOUNTP
sleep 1
exittest
