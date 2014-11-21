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
