#! /bin/bash

function extract_cppdefine {
	SYMBOL_REGEX=$1
	VALUE_REGEX=$2
	H_FILE=$3
	sed "s/#define $SYMBOL_REGEX[ \t]\+\($VALUE_REGEX\).*/\1/;t;d" $H_FILE
}

function max {
	echo "$@" | sort -nr | head -n1
}

OCTAL_VALUE_REGEX="0[0-7]\+"
DECIMAL_VALUE_REGEX="[0-9]\+"

function check_uapi_value {
	PREFIX=$1
	LMOD_SUFFIX=$2
	VALUE_REGEX=$3
	LMOD_H_FILE=$4
	KERNEL_H_FILE=$5
	function extract_values {
		SUFFIX=$1
		H_FILE=$2
		extract_cppdefine "$PREFIX$SUFFIX" "$VALUE_REGEX" "$H_FILE"
	}
	[[ ! -e $KERNEL_H_FILE ]] && return 1
	[[ ! -e $LMOD_H_FILE ]] && return 1
	KERNEL_VALUES=$(extract_values "[^\t ]*" $KERNEL_H_FILE)
	MAX_KERNEL_VALUE=$(max "$KERNEL_VALUES")
	LMOD_VALUE=$(extract_values "$LMOD_SUFFIX" $LMOD_H_FILE)
	# assert the custom flag is bigger than the biggest flag
	[[ $LMOD_VALUE -gt $MAX_KERNEL_VALUE ]]
	return $?
}

function report_gen {
	filename=$1
	echo "  GEN $filename"
}
