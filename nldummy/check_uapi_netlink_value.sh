#! /bin/bash

NLDUMMY_UAPI=$1

source $(dirname $0)/../env.sh

NETLINK_H=$KERNEL/include/uapi/linux/netlink.h

check_uapi_value "NETLINK_" "DUMMY" $DECIMAL_VALUE_REGEX \
		$NLDUMMY_UAPI $NETLINK_H
exit $?
