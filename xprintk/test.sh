#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
KERNLOG=/var/log/kern.log
LEVELS=(debug info notice warn err crit alert emerg)

err=0
cd $(dirname $0)
tmp=$(tempfile)
tail -f $KERNLOG -n0 > $tmp &
childpid=$!
insmod $MODULE.ko
sleep 1
kill $childpid
wait $childpid 2> /dev/null
kern_output=$(cat $tmp)
i=0
while read line; do
	level=${LEVELS[$i]}
	echo "$0: looking for message with level $level" 1>&2
	if ! $(echo $line | grep "$MODULE: $level" > /dev/null); then
		echo "$0: did not find message with level $level" 1>&2
		err=1
	fi
	let i++
done < $tmp
rm $tmp
rmmod $MODULE
exit $err
