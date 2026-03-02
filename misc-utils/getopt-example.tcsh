#!/bin/tcsh

# A small example script for using the getopt(1) program.
# This script will only work with tcsh(1).
# A similar script using the bash(1) language can be found
# as getopt-example.bash.

# Example input and output (from the tcsh prompt):
# ./getopt-example.tcsh -a --a-long \
#                       -barg_bs1 -b arg_bs2     --b-long=arg_bl1 --b-long arg_bl2 \
#                       -carg_cs1 -c not_arg_cs1 --c-long=arg_cl1 --c-long not_arg_cl2 \
#                       arg_p "string with quotes and space: '' \"\" "
# Option a
# Option a
# Option b, argument `arg_bs1'
# Option b, argument `arg_bs2'
# Option b, argument `arg_bl1'
# Option b, argument `arg_bl2'
# Option c, argument `arg_cs1'
# Option c, no argument
# Option c, argument `arg_cl1'
# Option c, no argument
# Remaining arguments:
# --> `not_arg_cs1'
# --> `not_arg_cl2'
# --> `arg_p'
# --> `string with quotes and space: '' "" '

# This is a bit tricky. We use a temp variable, to be able to check the
# return value of getopt (eval nukes it). argv contains the command arguments
# as a list. The ':q`  copies that list without doing any substitutions:
# each element of argv becomes a separate argument for getopt. The braces
# are needed because the result is also a list.
#
# Note: We can use '--long' instead of '--longoptions', because getopt(3) 
# allows unique abbreviations of long option names.
set temp=(`getopt -s tcsh -o ab:c:: --long a-long,b-long:,c-long:: -- $argv:q`)
if ($? != 0) then
  echo "Terminating..." >/dev/stderr
  exit 1
endif

# Now we do the eval part. As the result is a list, we need braces. But they
# must be quoted, because they must be evaluated when the eval is called.
# The 'q` stops doing any silly substitutions.
eval set argv=\($temp:q\)

while (1)
	switch($1:q)
	case -a:
	case --a-long:
		echo "Option a" ; shift
		breaksw;
	case -b:
	case --b-long:
		echo "Option b, argument "\`$2:q\' ; shift ; shift
		breaksw
	case -c:
	case --c-long:
		# c has an optional argument. As we are in quoted mode,
		# an empty parameter will be generated if its optional
		# argument is not found.

		if ($2:q == "") then
			echo "Option c, no argument"
		else
			echo "Option c, argument "\`$2:q\'
		endif
		shift; shift
		breaksw
	case --:
		shift
		break
	default:
		echo "Internal error!" ; exit 1
	endsw
end

echo "Remaining arguments:"
# foreach el ($argv:q) created problems for some tcsh-versions (at least
# 6.02). So we use another shift-loop here:
while ($#argv > 0)
	echo '--> '\`$1:q\'
	shift
end
