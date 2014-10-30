#! /bin/bash

DUMMIES_FILE="dummies.txt"
MODULE=elfsect

cd $(dirname $0)
dummies=$(cat $DUMMIES_FILE)

function __dummy_file_name {
	dummy=$1
	echo ${MODULE}_$dummy.gen
}

function list_objects {
	for dummy in $dummies; do
		echo $(__dummy_file_name $dummy).o
	done
}

function __create_dummy_source {
	dummy=$1
	echo "#include \"$MODULE.h\""
	echo
	echo "${MODULE}_define_dummy_func($dummy);"
}

function create_sources {
	for dummy in $dummies; do
		filename=$(__dummy_file_name $dummy).c
		echo "  GEN $filename"
		__create_dummy_source $dummy > $filename
	done
}

while (( $# )); do
	arg=$1
	shift
	case $arg in
		-l)
			list_objects
			;;
		-c)
			create_sources
			;;
	esac
done
exit 0
