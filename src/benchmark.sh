#!/usr/bin/env bash
# ShellChecked

set -eu
set -o pipefail

usage=$(cat <<EOF
Usage: $(basename "$0") [OPTION]... [PROGRAM [ARGUMENTS...]]

If not specified, the program(s) to run are read from "benchmark.input".

Available options are:

	-h
		Display this help and exit

	-r <n>
		Set number of repetitions to <n> (default is 10)
EOF
)

while getopts "hr:" arg; do
	case $arg in
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

benchmark() {
	local prog=$1
	shift
	local args=$*
	local logfile

	logfile="$(basename "$prog")"

	if [ -n "$args" ]; then
		logfile+="$(printf "_%s" "${args// /_}")"
	fi

	logfile+=".log"

	echo "./testrun.sh -r $repetitions $prog $args > $logfile" 1>&2
	./testrun.sh -r "$repetitions" "$prog" "$args" > "$logfile"
}

if [ $# -gt 0 ]; then
	benchmark "$@"
else
	# Read input from file
	while read -r line; do
		# Force word splitting of line
		benchmark $line
	done < benchmark.input
fi
