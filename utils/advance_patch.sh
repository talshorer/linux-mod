#! /bin/bash

FILE=$1
VERSION_MACRO="MODULE_VERSION"

old=$(sed "s/${VERSION_MACRO}(\"\(.*\)\");/\1/;t;d" $FILE)
[[ -z $old ]] && exit
eval "arr=($(echo $old | tr '.' ' '))"
let arr[$(( ${#arr[@]} - 1 ))]+=1
new=$(echo ${arr[@]} | tr ' ' '.')
sed "s/${VERSION_MACRO}(\"$old\")/${VERSION_MACRO}(\"$new\")/" -i $FILE
