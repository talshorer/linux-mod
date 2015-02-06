#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
INTERPRETER="./bf.out"
BF_FILE="./hello.bf"
EXPECTED_OUTPUT="Hello World!"

execv()
{
	./execv.out $1
}

echo_trying_exec()
{
	echo "$0: trying to execute brainfuck binary $1 loading $MODULE" 1>&2
}

err=0
cd $(dirname $0)
output=$(execv $BF_FILE 2>&1)
echo_trying_exec "before"
if ! echo $output | grep "Exec format error" > /dev/null; then
	echo "$0: succeeded executing $BF_FILE before loading $MODULE" 1>&2
	err=1
fi
insmod $MODULE.ko interpreter=$(realpath $INTERPRETER)
echo_trying_exec "after"
output=$(execv $BF_FILE 2> /dev/null)
if [[ $? != 0 ]]; then
	echo "$0: failed executing $BF_FILE" 1>&2
	err=1
fi
if [[ "$output" != "$EXPECTED_OUTPUT" ]]; then
	echo "$0: $BF_FILE did not emit expected output" 1>&2
	echo "$0: expected  \"$EXPECTED_OUTPUT\"" 1>&2
	echo "$0: actual \"$output\"" 1>&2
	err=1
fi
rmmod $MODULE
exit $err
