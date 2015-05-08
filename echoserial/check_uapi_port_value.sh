#! /bin/bash

ECHOSERIAL_UAPI=$1

source $(dirname $0)/../env.sh

SERIAL_CORE_H=$KERNEL/include/uapi/linux/serial_core.h

check_uapi_value "PORT_" "ECHOSERIAL" $DECIMAL_VALUE_REGEX \
		$ECHOSERIAL_UAPI $SERIAL_CORE_H
exit $?
