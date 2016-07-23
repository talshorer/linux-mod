#! /bin/bash

SERIOREPEATER_UAPI=$1

source $(dirname $0)/../env.sh

SERIO_H=$KERNEL/include/uapi/linux/serio.h

check_uapi_value "SERIO_" "REPEATER" $HEXADECIMAL_VALUE_REGEX \
		$SERIOREPEATER_UAPI $SERIO_H
exit $?
