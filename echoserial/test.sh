#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NPORTS=4
BSIZE=$(( 0x1000 ))

err=0
cd $(dirname $0)
insmod $MODULE.ko nports=$NPORTS bsize=$BSIZE
./test.out $NPORTS $BSIZE || err=1
rmmod $MODULE
exit $err
