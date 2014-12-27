#! /bin/bash

USAGE="$0 MODULE [-mcht]"
TEMPLATE=template
TEMPLATE_DIR=utils/templates

__replacements()
{
	fullpath=$1
	filename=$(basename ${fullpath%.*})
	sed -i "s/__MODULE_NAME_LOWERCASE/$MODULE/g"      $fullpath
	sed -i "s/__MODULE_NAME_PUPPERCASE/${MODULE^^}/g" $fullpath
	sed -i "s/__FILE_NAME_LOWERCASE/$filename/g"      $fullpath
	sed -i "s/__FILE_NAME_UPPERCASE/${filename^^}/g"  $fullpath
}

__from_template()
{
	template=$TEMPLATE_DIR/$1
	filepath=$2
	cp $template $filepath
	__replacements $filepath
}

__list_sources()
{
	ls $MODULE/*.c 2> /dev/null
}

__create_file()
{
	suffix=$1
	fullpath=$MODULE/$2
	if [[ -e $fullpath ]]; then
		echo "$0: $fullpath already exists"
		exit 1
	fi
	if [[ $suffix == c ]] ; then
		func=create_source
	else
		func=create_header
	fi
	eval $func $fullpath
}

create_makefile()
{
	makefile=$MODULE/Makefile
	if [[ -e $makefile ]]; then
		echo "$0: $makefile already exists"
		exit 1
	fi
	sources=$(__list_sources)
	n_sources=$(echo -n $sources | wc -w)
	if [[ $n_sources == 0 ]]; then
		echo "$0: can't create a makefile without source files"
		exit 1
	fi
	echo "$0: creating makefile $makefile"
	touch $makefile
	echo "obj-m := $MODULE.o" >> $makefile
	if [[ $n_sources > 1 ]]; then
		echo -n "$MODULE-y :=" >> $makefile
		for fullpath in $sources; do
			sourcefile=${fullpath#$MODULE/}
			objfile=${sourcefile%.c}.o
			echo -n " $objfile" >> $makefile
		done
		echo >> $makefile
	fi
	echo >> $makefile
	cat $TEMPLATE_DIR/$TEMPLATE.mk >> $makefile
}

create_test()
{
	TEST=test.sh
	testfile=$MODULE/$TEST
	echo "$0: creating test $testfile"
	__from_template ${TEMPLATE}_$TEST $testfile
}

create_source()
{
	fullpath=$1
	echo "$0: creating source $fullpath"
	if [[ $(__list_sources | wc -l) == 0 ]]; then
		__from_template $TEMPLATE.c $fullpath
	else
		echo "/* code */" > $fullpath
	fi
}

create_header()
{
	fullpath=$1
	echo "$0: creating header $fullpath"
	__from_template $TEMPLATE.h $fullpath
}

print_usage()
{
	echo "Usage: $USAGE" 1>&2
	exit 1
}

MODULE=$1
shift

if [[ ! -e $MODULE ]]; then
	echo "$0: creating directory $MODULE"
	mkdir $MODULE
fi

while (( $# )); do
	arg=$1
	shift
	case $arg in
		-m)
			create_makefile
			;;
		-t)
			create_test
			;;
		-c|-h)
			suffix=${arg#-}
			if [[ $1 != -* && ! -z $1 ]]; then
				f=$1.$suffix
				shift
			else
				f=$MODULE.$suffix
			fi
			__create_file $suffix $f
			;;
		*)
			print_usage
	esac
done
