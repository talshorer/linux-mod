#! /bin/sh

MATCH="/sys/bus/usb/drivers/usbtunnel-host/match"

usage()
{
	echo "usage: $0 -a PORT UDC" >&2
	echo "       $0 -d PORT" >&2
	echo "       $0 -l" >&2
	exit 1
}

[ $# == 0 ] && usage

case $1 in
	-a)
		[ $# != 3 ] && usage
		echo "add $2 $3" > $MATCH
		exit $?
		;;
	-d)
		[ $# != 2 ] && usage
		echo "del $2" > $MATCH
		exit $?
		;;
	-l)
		[ $# != 1 ] && usage
		cat $MATCH
		exit $?
		;;
esac
