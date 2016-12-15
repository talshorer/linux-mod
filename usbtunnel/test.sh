#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))

err=0
cd $(dirname $0)
insmod $MODULE.ko
modprobe dummy_hcd num=2
# test logic
err=1
rmmod dummy_hcd
rmmod $MODULE
exit $err
