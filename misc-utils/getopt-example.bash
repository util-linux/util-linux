#!/bin/bash

# A small example script for using the getopt(1) program.
# This script will only work with bash(1).
# A similar script using the tcsh(1) language can be found
# as getopt-example.tcsh.

# Example input and output (from the bash prompt):
#
# ./getopt-example.bash -a --a-long \
#                       -barg_bs1 -b arg_bs2     --b-long=arg_bl1 --b-long arg_bl2 \
#                       -carg_cs1 -c not_arg_cs1 --c-long=arg_cl1 --c-long not_arg_cl2 \
#                       arg_p "string with quotes and space: '' \"\" "
# Option a
# Option a
# Option b, argument 'arg_bs1'
# Option b, argument 'arg_bs2'
# Option b, argument 'arg_bl1'
# Option b, argument 'arg_bl2'
# Option c, argument 'arg_cs1'
# Option c, no argument
# Option c, argument 'arg_cl1'
# Option c, no argument
# Remaining arguments:
# --> 'not_arg_cs1'
# --> 'not_arg_cl2'
# --> 'arg_p'
# --> 'string with quotes and space: '' "" '

# Note that we use "$@" to let each command-line parameter expand to a
# separate word. The quotes around "$@" are essential!
# We need TEMP as the 'eval set --' would nuke the return value of getopt.
#
# Note: We can use '--long' instead of '--longoptions', because getopt(3) 
# allows unique abbreviations of long option names.
TEMP=$(getopt -o 'ab:c::' --long 'a-long,b-long:,c-long::' -n 'example.bash' -- "$@")

if [ $? -ne 0 ]; then
	echo 'Terminating...' >&2
	exit 1
fi

# Note the quotes around "$TEMP": they are essential!
eval set -- "$TEMP"
unset TEMP

while true; do
	case "$1" in
		'-a'|'--a-long')
			echo 'Option a'
			shift
			continue
		;;
		'-b'|'--b-long')
			echo "Option b, argument '$2'"
			shift 2
			continue
		;;
		'-c'|'--c-long')
			# c has an optional argument. As we are in quoted mode,
			# an empty parameter will be generated if its optional
			# argument is not found.
			case "$2" in
				'')
					echo 'Option c, no argument'
				;;
				*)
					echo "Option c, argument '$2'"
				;;
			esac
			shift 2
			continue
		;;
		'--')
			shift
			break
		;;
		*)
			echo 'Internal error!' >&2
			exit 1
		;;
	esac
done

echo 'Remaining arguments:'
for arg; do
	echo "--> '$arg'"
done
