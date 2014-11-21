#! /bin/bash

NLDUMMY_UAPI=$1
KERNEL=$2

source $(dirname $0)/../env.sh

function extract_netlink_values {
	NL_SUFFIX=$1
	H_FILE=$2
	extract_cppdefine "NETLINK_$NL_SUFFIX" "[0-9]\+" "$H_FILE"
}

NETLINK_H=$KERNEL/include/uapi/linux/netlink.h
NETLINK_VALUES=$(extract_netlink_values ".*" $NETLINK_H)
MAX_NETLINK_VALUE=$(max "$NETLINK_VALUES")

NETLINK_DUMMY=$(extract_netlink_values "DUMMY" $NLDUMMY_UAPI)

# assert the custom flag is bigger than the biggest flag
[[ $NETLINK_DUMMY -gt $MAX_NETLINK_VALUE ]]
exit $?
