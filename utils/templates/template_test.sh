#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))

err=0
cd $(dirname $0)
insmod $MODULE.ko
# test logic
rmmod $MODULE
exit $err
