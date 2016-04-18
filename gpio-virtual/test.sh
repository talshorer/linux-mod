#! /bin/bash

MODULE=$(basename $(dirname $(realpath $0)))
KBUILD_MODNAME=$(echo $MODULE | tr "-" "_")
NCHIPS=4
CHIP_NPINS=32
MODULE_SYSFS=/sys/class/$KBUILD_MODNAME
GPIO_SYSFS=/sys/class/gpio
IN=in
OUT=out
DIRECTION=direction
VALUE=value

get_gpio_sysfs()
{
	gpioname=$1
	echo $GPIO_SYSFS/$gpioname
}

check_gpio_value()
{
	gpioname=$1
	expected=$2
	gpio_sysfs=$(get_gpio_sysfs $gpioname)
	actual=$(cat $gpio_sysfs/$VALUE)
	if [[ $actual != $expected ]]; then
		echo -n "$0: unexpected value for $gpioname. " 1>&2
		echo "expected $expected, actual $actual" 1>&2
		return 1
	fi
	return 0
}

err=0
cd $(dirname $0)
insmod $MODULE.ko nchips=$NCHIPS chip_npins=$CHIP_NPINS
for i in $(seq 0 $(( $NCHIPS - 1 ))); do
	chip=${KBUILD_MODNAME}$i
	for gpiochip in $(ls -1 $MODULE_SYSFS/$chip | grep gpiochip); do
		[[ -e $MODULE_SYSFS/$chip/$gpiochip/device ]] && break
	done
	echo "$0: running test on chip $chip ($gpiochip)" 1>&2
	gpiochip_sysfs=$GPIO_SYSFS/$gpiochip
	if [[ ! -e $gpiochip_sysfs ]]; then
		echo "$0: sysfs dir for $chip not found" 1>&2
		err=1
		continue
	fi
	base=$(cat $gpiochip_sysfs/base)
	ngpio=$(cat $gpiochip_sysfs/ngpio)
	if [[ $ngpio != $CHIP_NPINS ]]; then
		echo -n "$0: unexpected $ngpio for $chip ($gpiochip). " 1>&2
		echo "expected $CHIP_NPINS, actual $ngpio" 1>&2
		err=1
	fi
	for j in $(seq 0 $(( $ngpio - 1 ))); do
		gpio=$(( $base + $j ))
		gpioname=gpio$gpio
		echo $gpio > $GPIO_SYSFS/export
		gpio_sysfs=$(get_gpio_sysfs $gpioname)
		if [[ "$(cat $gpio_sysfs/$DIRECTION)" != "$IN" ]]; then
			echo -n "$0: $gpioname was not initializated " 1>&2
			echo "as input" 1>&2
			err=1
			echo $IN > $gpio_sysfs/$DIRECTION
		fi
		# gpio pins should read 0 when set to input
		check_gpio_value $gpioname 0 || err=1
		echo $OUT > $gpio_sysfs/$DIRECTION
		for value in 0 1; do
			echo $value > $gpio_sysfs/$VALUE
			check_gpio_value $gpioname $value || err=1
		done
		# return to input
		echo $IN > $gpio_sysfs/$DIRECTION
		echo $gpio > $GPIO_SYSFS/unexport
	done
done
rmmod $MODULE
exit $err
