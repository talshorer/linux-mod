#! /bin/bash

MODULE=__MODULE_NAME_LOWERCASE

err=0
insmod $MODULE.ko
# test logic
rmmod $MODULE
exit $err
