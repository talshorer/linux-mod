#! /bin/bash

INTERCEPTOR_UAPI=$1
KERNEL=$2

source $(dirname $0)/../env.sh

function extract_o_values {
	SYMBOL_REGEX=$1
	H_FILE=$2
	extract_cppdefine "$SYMBOL_REGEX" "[0-7]\+" "$H_FILE"
}

FCNTL_H=$KERNEL/include/uapi/asm-generic/fcntl.h
O_VALUES=$(extract_o_values "_*O_.*" $FCNTL_H)
MAX_O_VALUE=$(max "$O_VALUES")

O_STRLEN=$(extract_o_values "INTERCEPTOR_O_STRLEN" $INTERCEPTOR_UAPI)

# assert the custom flag is bigger than the biggest flag and is a power of 2
[[ $O_STRLEN -gt $MAX_O_VALUE && $(( $O_STRLEN & ($O_STRLEN - 1) )) == 0 ]]
exit $?
