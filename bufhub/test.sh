#! /bin/bash

MODULE=bufhub

err=0
insmod $MODULE.ko
./test.out || err=1
rmmod $MODULE
exit $err