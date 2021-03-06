#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))

err=0
cd $(dirname $0)
insmod $MODULE.ko
if [[ $? != 0 ]]; then
	echo "$0: failed to load module $MODULE"
	err=1
else
	echo "$0: successfully loaded module $MODULE"
fi
rmmod $MODULE
exit $err
