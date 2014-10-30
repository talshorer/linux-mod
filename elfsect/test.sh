#! /bin/bash

MODULE=elfsect
SECTION_NAME=.dummies
DUMMIES_FILENAME=dummies
LOCAL_DUMMIES="$DUMMIES_FILENAME.txt"
DEBUGFS_DUMMIES="/sys/kernel/debug/$MODULE/$DUMMIES_FILENAME"

err=0
cd $(dirname $0)
insmod $MODULE.ko
if [[ -e /sys/module/$MODULE/sections/$SECTION_NAME ]]; then
	exists="exists"
else
	exists="doesn't exist"
	err=1
fi
echo "$0: section $SECTION_NAME $exists in module $MODULE" 1>&2
if cmp -s $LOCAL_DUMMIES $DEBUGFS_DUMMIES; then
	match=""
else
	match="don't "
	err=1
fi
echo "$0: $DUMMIES_FILENAME files in local directory and debufgs ${match}match"
rmmod $MODULE
exit $err
