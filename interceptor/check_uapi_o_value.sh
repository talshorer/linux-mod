#! /bin/bash

INTERCEPTOR_UAPI=$1

source $(dirname $0)/../env.sh

FCNTL_H=$KERNEL/include/uapi/asm-generic/fcntl.h
PREFIX="_*O_"
LMOD_SUFFIX="STRLEN"

check_uapi_value $PREFIX $LMOD_SUFFIX $OCTAL_VALUE_REGEX \
		$INTERCEPTOR_UAPI $FCNTL_H || exit 1

O_STRLEN=$(extract_cppdefine "$PREFIX$LMOD_SUFFIX" $OCTAL_VALUE_REGEX \
		$INTERCEPTOR_UAPI)

# assert the custom flag is a power of 2
[[ $(( $O_STRLEN & ($O_STRLEN - 1) )) == 0 ]]
exit $?
