#!/usr/bin/env bash
# ShellChecked

set -eu
set -o pipefail

usage=$(cat <<EOF
Usage: $(basename "$0") test.input [repetitions]

If not specified, repetitions is set to 10.
EOF
)

if [ "$#" -lt 1 ]; then
	echo "$usage"
	exit 0
fi

# Optional argument
# Default is ten repetitions for each benchmark
repetitions=${2:-10}

RESET="\e[0m"
BOLD="\e[1m"
FAIL="\e[31m"
PASS="\e[32m"

print_success() {
	echo -ne "$BOLD$PASS \u2714$RESET"
}

print_failure() {
	echo -ne "$BOLD$FAIL \u2718$RESET"
}

testrun() {
	local prog=$1
	local success=true
	shift

	echo -n "$prog $*: "
	for _ in $(seq 1 "$repetitions"); do
		echo -n "."
		if ! "$prog" "$@" > /dev/null #2>&1
		then
			success=false
			break
		fi
	done
	if [ "$success" = true ]; then
		print_success
	else
		print_failure
	fi
	echo
}

while read -r line; do
	# Force word splitting of line
	testrun $line
done < "$1"
