#! /bin/bash

MODULE=section
SECTION_NAME=.dummies

err=0
cd $(dirname $0)
insmod $MODULE.ko
if [[ -e /sys/module/$MODULE/sections/$SECTION_NAME ]]; then
	exists="exists"
else
	exists="doesn't exist"
	err=1
fi
echo "$0: section $SECTION_NAME $exists in module $MODULE" 1>&2
rmmod $MODULE
exit $err
