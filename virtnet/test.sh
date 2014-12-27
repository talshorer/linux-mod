#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
NIFACES=4
BACKENDS="lb chr"
IFACE_BASE_NAME=virt

err=0
cd $(dirname $0)
for backend in $BACKENDS; do
	insmod $MODULE.ko nifaces=$NIFACES backend=$backend
	for i in $(seq 0 $(( $NIFACES - 1 ))); do
		iface=${IFACE_BASE_NAME}$i
		echo -n "$0: running test with backend $backend and " 1>&2
		echo "interface $iface" 1>&2
		ip link set $iface up
		./test.out $backend $i || err=1
		ip link set $iface down
	done
	rmmod $MODULE
done
exit $err
