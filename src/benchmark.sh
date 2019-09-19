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

	-s
		Print summary statistics
EOF
)

print_stats=0

while getopts "hr:s" arg; do
	case $arg in
		r)
			repetitions=$OPTARG
			;;
		s)
			print_stats=1
			;;
		h|*)
			echo "$usage"
			exit 0
			;;
	esac
done

# If `NUM_THREADS` is not defined, get the number of CPUs from `lscpu`
num_threads=${NUM_THREADS:-$(lscpu | grep ^CPU\(s\) | tr -d ' ' | cut -d ':' -f 2)}
repetitions=${repetitions:-10}
shift $((OPTIND-1))

if [ "$print_stats" = 1 ]; then
	if [ -x "$(command -v column)" ]; then
		tableize="column -s ',' -o ' | ' -t"
	else
		tableize="tr ',' '\t'"
	fi
fi

benchmark() {
	local prog=$1
	shift
	local args=$*
	local logfile

	logfile="$(basename "$prog")"

	if [ -n "$args" ]; then
		logfile+="$(printf "_%s" "${args// /_}")"
	fi

	logfile+="$(printf "_%02d.log" "$num_threads")"

	echo -n "NUM_THREADS=$num_threads " 1>&2

	if [ "$print_stats" = 1 ]; then
		./testrun.sh -r "$repetitions" "$prog" "$args" \
			| tee "$logfile" \
			| grep "[Ee]lapsed" \
			| cut -d ' ' -f 4 \
			| utils/stats.lua \
			| eval "$tableize"
	else
		./testrun.sh -r "$repetitions" "$prog" "$args" > "$logfile"
	fi
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
