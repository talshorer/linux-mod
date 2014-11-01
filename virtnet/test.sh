#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NIFACES=4
BACKENDS="lb chr"
IFACE_BASE_NAME=virt

err=0
cd $(dirname $0)
for backend in $BACKENDS; do
	insmod $MODULE.ko nifaces=$NIFACES backend=$backend
	for ((i=0; i<$NIFACES; i++)); do
		iface=${IFACE_BASE_NAME}$i
		echo "$0: running test with backend $backend and interface $iface" 1>&2
		ip link set $iface up
		./test.out $backend $i || err=1
		ip link set $iface down
	done
	rmmod $MODULE
done
exit $err
