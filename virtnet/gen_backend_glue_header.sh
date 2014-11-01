#! /bin/bash

backends=""
for objfile in "$@"; do
	backends="$backends ${objfile%.o}"
done

cat << END_CAT
/* file generated by $0. do not edit */
#include "virtnet.h"

END_CAT

echo "/* backends */"
for backend in $backends; do
	echo "extern struct virtnet_backend_ops ${backend}_backend_ops;"
done
echo

cat << END_CAT
/* glue */
#define VIRTNET_BACKEND_GLUE() \\
END_CAT

for backend in $backends; do
	name=${backend#virtnet_}
	echo -e "\tVIRTNET_BACKEND_ENTRY($name), \\"
done
echo
