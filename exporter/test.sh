#! /bin/bash

EXPORTER=exporter
IMPORTER=importer

__test()
{
	echo -n "$0: checking that $1... " 1>&2
	if test "$3" == "--pos"; then
		expected=0
	elif test "$3" == "--neg"; then
		expected=1
	fi
	eval $2 2>/dev/null
	exitcode=$(( ! ! $? ))
	test "$exitcode" == "$expected"
	ret=$?
	if test $ret == 0; then
		echo "success" 1>&2
	else
		echo "failed" 1>&2
	fi
	return $ret
}

positive_test()
{
	__test "$@" --pos
	return $?
}

negative_test()
{
	__test "$@" --neg
	return $?
}

err=0
cd $(dirname $0)
negative_test "$IMPORTER can't be loaded without $EXPORTER" \
		"insmod $IMPORTER.ko" || err=1
positive_test "$IMPORTER can be loaded with $EXPORTER" \
		"insmod $EXPORTER.ko && insmod $IMPORTER.ko" || err=1
negative_test "$EXPORTER can't be removed while $IMPORTER is loaded" \
		"rmmod $EXPORTER" || err=1
positive_test "$EXPORTER can be removed after $IMPORTER is removed" \
		"rmmod $IMPORTER && rmmod $EXPORTER" || err=1
exit $err
