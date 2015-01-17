#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
MOUNTP=mp
MAXDEPTH=4

exittest()
{
	rmmod $MODULE
	exit $err
}

__check_subdirs()
{
	depth=$1
	local lsoutput=$(ls -1)
	for i in $(seq 0 $(( $MAXDEPTH - $depth - 1 ))); do
		subdir=sub0x$(printf %02x $i)
		if ! echo "$lsoutput" | grep $subdir &> /dev/null; then
			echo "$0: missing subdir $(pwd)/$subdir in ls" 1>&2
			err=1
		fi
		if [[ -e $subdir ]]; then
			check_subdirs $subdir $(( $depth + 1 ))
		else
			echo "$0: missing subdir $(pwd)/$subdir" 1>&2
			err=1
		fi
	done
}

check_subdirs()
{
	cd $1
	__check_subdirs $2
	cd ..
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
check_subdirs $MOUNTP 0
umount $MOUNTP
sleep 1
exittest
