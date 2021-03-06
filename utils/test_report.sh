#! /bin/bash

mkdir -p $1
TARGET=$(realpath $1)
VM_USER=$2
VM_DIR="/tmp"
UTILS=$(dirname $0)

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
		cmd="$UTILS/send_vm.sh $m $VM_USER:$VM_DIR"
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
	echo "running checkpatch"
	eval "$UTILS/run_checkpatch.sh $m/*" || err=1
	echo $SEP
	echo "err=$err"
	let failure+=$err
}

if [[ -z $KERNEL ]]; then
	echo "KERNEL is not set"
	exit 1
fi

if [[ -z "$modules" ]]; then modules=$($UTILS/all_modules.sh); fi

for m in $modules; do
	test_module $m 2>&1 | tee /$TARGET/$m
done
exit $failure
