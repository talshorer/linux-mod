#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NOBJS=8
MODULE_CONFIGFS=/sys/kernel/config/$MODULE
MODULE_SYSFS=/sys/kernel/$MODULE

__objname()
{
	echo ${MODULE}$1
}

__obj_configfs()
{
	echo $MODULE_CONFIGFS/$1
}

__obj_sysfs()
{
	echo $MODULE_SYSFS/$1
}

err=0
cd $(dirname $0)
insmod $MODULE.ko
echo "$0: creating and testing $NOBJS objects" 1>&2
for i in $(seq 0 $(( $NOBJS - 1 ))); do
	obj=$(__objname $i)
	obj_configfs=$(__obj_configfs $obj)
	obj_sysfs=$(__obj_sysfs $obj)
	mkdir $obj_configfs
	if [[ ! -e $obj_sysfs ]]; then
		echo "$0: $obj missing from sysfs" 1>&2
		err=1
	fi
	# TODO attr
done
echo "$0: destroying all objects" 2>&1
for i in $(seq 0 $(( $NOBJS - 1 ))); do
	obj=$(__objname $i)
	obj_configfs=$(__obj_configfs $obj)
	obj_sysfs=$(__obj_sysfs $obj)
	rm -rf $obj_configfs
	if [[ -e $obj_sysfs ]]; then
		echo "$0: $obj still in sysfs" 1>&2
		err=1
	fi
done
rmmod $MODULE
exit $err
