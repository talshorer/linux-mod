#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NOBJS=8
MODULE_CONFIGFS=/sys/kernel/config/$MODULE
MODULE_SYSFS=/sys/kernel/$MODULE
VALUES=$(seq 0 0x80 $(( 0x1000 - 1 )))
NVALUES=$(echo $VALUES | wc -w)
ATTR=attr

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

readback_test()
{
	obj=$1
	expected=$2
	lerr=0
	for fs in configfs sysfs; do
		local attr_file=$(__obj_$fs $obj)/$ATTR
		actual=$(cat $attr_file)
		if [[ "$actual" != "$expected" ]]; then
			echo -n "$0: failed readback test on $attr_file " 1>&2
			echo "expected $expected, actual $actual" 1>&2
			lerr=1
		fi
	done
	return $lerr
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
	echo "$0: running readback test on $obj with $NVALUES values" 1>&2
	attr_file=$obj_configfs/$ATTR
	readback_test $obj 0 || err=1
	for v in $VALUES; do
		eval "echo $v > $attr_file" || err=1
		readback_test $obj $v || err=1
	done
done
echo "$0: destroying all objects" 2>&1
for i in $(seq 0 $(( $NOBJS - 1 ))); do
	obj=$(__objname $i)
	obj_configfs=$(__obj_configfs $obj)
	obj_sysfs=$(__obj_sysfs $obj)
	rmdir $obj_configfs
	if [[ -e $obj_sysfs ]]; then
		echo "$0: $obj still in sysfs" 1>&2
		err=1
	fi
done
rmmod $MODULE
exit $err
