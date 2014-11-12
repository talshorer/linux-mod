#! /bin/bash

INTERCEPTOR_UAPI=$1
KERNEL=$2

function extract_o_values {
	SYMBOL_REGEX=$1
	H_FILE=$2
	sed "s/#define ${SYMBOL_REGEX}[ \t]\+\([0-7]\+\).*/\1/;t;d" $H_FILE
}

FCNTL_H=$KERNEL/include/uapi/asm-generic/fcntl.h
O_VALUES=$(extract_o_values "_*O_.*" $FCNTL_H)
MAX_O_VALUE=$(echo "$O_VALUES" | sort -nr | head -n1)

O_STRLEN=$(extract_o_values "INTERCEPTOR_O_STRLEN" $INTERCEPTOR_UAPI)

# assert the custom flag is bigger than the biggest flag and is a power of 2
[[ $O_STRLEN -gt $MAX_O_VALUE && $(( $O_STRLEN & ($O_STRLEN - 1) )) == 0 ]]
exit $?
