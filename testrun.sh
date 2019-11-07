#!/usr/bin/env bash
# ShellChecked

set -eu
set -o pipefail

usage=$(cat <<EOF
Usage: $(basename "$0") [OPTION]... [PROGRAM [ARGUMENTS...]]

If not specified, the program(s) to run are read from "testrun.input".

Available options are:

	-d
		Discard program output

	-h
		Display this help and exit

	-r <n>
		Set number of repetitions to <n> (default is 10)
EOF
)

discard_output=0

while getopts "dhr:" arg; do
	case $arg in
		d)
			discard_output=1
			;;
		r)
			repetitions=$OPTARG
			;;
		h|*)
			echo "$usage"
			exit 0
			;;
	esac
done

repetitions=${repetitions:-10}
shift $((OPTIND-1))

RESET="\e[0m"
BOLD="\e[1m"
FAIL="\e[31m"
PASS="\e[32m"

print_success() {
	echo -e "$BOLD$PASS \u2714$RESET" 1>&2
}

print_failure() {
	echo -e "$BOLD$FAIL \u2718$RESET" 1>&2
}

testrun() {
	local prog=$1
	local success=true
	shift

	echo -n "$prog $*: " 1>&2
	for _ in $(seq 1 "$repetitions"); do
		echo -n "." 1>&2
		if [ "$discard_output" = 1 ]; then
			if ! eval "$prog $*" > /dev/null #2>&1
			then
				success=false
				break
			fi
		else
			if ! eval "$prog $*"; then
				success=false
				break
			fi
		fi
	done
	if [ "$success" = true ]; then
		print_success
	else
		print_failure
	fi
}

if [ $# -gt 0 ]; then
	testrun "$@"
else
	# Read input from file
	while read -r line; do
		# Force word splitting of line
		testrun $line
	done < testrun.input
fi
