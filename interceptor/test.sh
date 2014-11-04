#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
DATA="Hello, World!"

err=0
cd $(dirname $0)
insmod $MODULE.ko
expected=$(echo -n "$DATA" | wc -c)
actual=$(./test.out "$DATA")
if [[ "$expected" == "$actual" ]]; then
	returned="returned"
else
	returned="didn't return"
	err=1
fi
echo "$0: intercepted syscall $returned expected value" 1>&2
if [[ $err != 0 ]]; then
	echo "$0: expected $expected, actual $actual" 1>&2
fi
rmmod $MODULE
exit $err
