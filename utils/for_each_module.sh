for MODULE in $(ls); do
	if [[ ! -d $MODULE || "$MODULE" == "utils" ]]; then
		continue
	fi
	export $MODULE
	eval "$@"
done
