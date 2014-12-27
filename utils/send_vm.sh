#! /bin/bash

LOCAL_DIR=$1
TARGET=$2
TARGET_DIR=$TARGET/$(basename $LOCAL_DIR)

function upload {
	scp $1 $TARGET_DIR/$(basename $1)
}

function local_ls {
	pattern=$1
	ls $LOCAL_DIR/$pattern 2> /dev/null
}

ssh ${TARGET%:*} "mkdir -p ${TARGET_DIR#*:}"

for pattern in "*.ko" "test.sh" "*.out"; do
	for f in $(local_ls $pattern); do upload $f; done
done

TESTDEP=$LOCAL_DIR/testdep
if [[ -e $TESTDEP ]]; then
	for f in $(cat $TESTDEP); do upload $LOCAL_DIR/$f; done
fi
