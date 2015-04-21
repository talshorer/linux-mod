#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
KERNLOG=/var/log/kern.log
[[ ! -e $KERNLOG ]] && KERNLOG=/var/log/messages
LEVELS="debug info notice warn err crit alert emerg"

err=0
cd $(dirname $0)
tmp=$(mktemp)
tail -f $KERNLOG -n0 > $tmp &
childpid=$!
insmod $MODULE.ko
sleep 1
kill $childpid
wait $childpid 2> /dev/null
for level in $LEVELS; do
	echo "$0: looking for message with level $level" 1>&2
	read line || err=1
	if ! $(echo $line | grep "$MODULE: $level" > /dev/null); then
		echo "$0: did not find message with level $level" 1>&2
		echo "$0: line was: \"$line\"" 1>&2
		err=1
	fi
done < $tmp
rm $tmp
rmmod $MODULE
exit $err
