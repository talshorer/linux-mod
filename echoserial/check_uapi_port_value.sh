#! /bin/bash

ECHOSERIAL_UAPI=$1
KERNEL=$2

source $(dirname $0)/../env.sh

function extract_port_values {
	PORT_SUFFIX=$1
	H_FILE=$2
	extract_cppdefine "PORT_$PORT_SUFFIX" "[0-9]\+" "$H_FILE"
}

SERIAL_CORE_H=$KERNEL/include/uapi/linux/serial_core.h
SERIAL_CORE_VALUES=$(extract_port_values "[^\t ]*" $SERIAL_CORE_H)
MAX_SERIAL_CORE_VALUE=$(max "$SERIAL_CORE_VALUES")

PORT_ECHOSERIAL=$(extract_port_values "ECHOSERIAL" $ECHOSERIAL_UAPI)

# assert the custom flag is bigger than the biggest flag
[[ $PORT_ECHOSERIAL -gt $MAX_SERIAL_CORE_VALUE ]]
exit $?
