#! /bin/bash

MODULE=simpleattr
NDEVICES=4
CLASS_SYSFS=classlink
VALUES=$(seq 0 0x80 $(( 0x10000 - 1 )))
NVALUES=$(echo $VALUES | wc -w)

function readback_test {
	attr_file=$1
	expected=$2
	actual=$(cat $attr_file)
		if [[ "$actual" != "$expected" ]]; then
			echo -n "$0: failed readback test. " 1>&2
			echo "expected $expected, actual $actual" 1>&2
		fi
}

err=0
insmod $MODULE.ko ndevices=$NDEVICES
for (( i=0; i<$NDEVICES; i++ )); do
	device=${MODULE}$i
	attr_file=$CLASS_SYSFS/$device/attr
	echo "$0: running test with device $device and $NVALUES values" 1>&2
	readback_test $attr_file 0
	for v in $VALUES; do
		echo $v > $attr_file
		readback_test $attr_file $v
	done
done
rmmod $MODULE
exit $err
