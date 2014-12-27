#! /bin/bash

mkdir -p $1
TARGET=$(realpath $1)
VM_USER=$2
VM_DIR="/tmp"

shift; shift
modules="$@"

failure=0

function test_module {
	m=$1
	SEP="======================================="
	err=0
	echo $SEP
	make -C $m all || err=1
	echo $SEP
	if [[ $err == 0 ]]; then
		cmd="utils/send_vm.sh $m $VM_USER:$VM_DIR"
		echo $cmd
		eval $cmd || err=1
		echo $SEP
		cmd="ssh $VM_USER \"sh $VM_DIR/$m/test.sh\""
		echo $cmd
		eval $cmd || err=1
		echo $SEP
	fi
		make -C $m clean
		echo $SEP
	echo "err=$err"
	let failure+=$err
}

cd $(dirname $0)/..

if [[ -z "$modules" ]]; then modules=$(utils/all_modules.sh); fi

for m in $modules; do
	test_module $m 2>&1 | tee /$TARGET/$m
done
exit $failure
