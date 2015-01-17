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
	local depth=$1
	local lsoutput=$(ls -1)
	local depthfile=$(pwd)/depth
	local actual=$(cat $depthfile)
	local expected=$(printf 0x%02x $depth)
	if [[ "$expected" != "$actual" ]]; then
		echo -n "$0: unexpected data in depth file $depthfile " 1>&2
		echo "expected $expected actual $actual" 1>&2
		err=1
	fi
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
