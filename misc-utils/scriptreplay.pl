#!@PERL@ -w

# "script -t" will output a typescript with timings
# this script "scriptreplay" replays it
# run pod2man on it to get a man page

=head1 NAME

scriptreplay - play back typescripts, using timing information

=head1 SYNOPSIS

scriptreplay timingfile [typescript [divisor]]

=head1 DESCRIPTION

This program replays a typescript, using timing information to ensure that
output happens at the same speed as it originally appeared when the script
was recorded. It is only guaranteed to work properly if run on the same
terminal the script was recorded on.

The timings information is what script outputs to standard error if it is
run with the -t parameter.

By default, the typescript to display is assumed to be named "typescript",
but other filenames may be specified, as the second parameter.

If the third parameter exits, it is used as a time divisor. For example,
specifying a divisor of 2 makes the script be replayed twice as fast.

=head1 EXAMPLE

 % script -t 2> timingfile
 Script started, file is typescript
 % ls
 <etc, etc>
 % exit
 Script done, file is typescript
 % scriptreplay timingfile

=cut

use strict;
$|=1;
open (TIMING, shift)
        or die "cannot read timing info: $!";
open (TYPESCRIPT, shift || 'typescript')
        or die "cannot read typescript: $!";
my $divisor=shift || 1;

# Read starting timestamp line and ignore.
<TYPESCRIPT>;

my $block;
my $oldblock='';
while (<TIMING>) {
        my ($delay, $blocksize)=split ' ', $_, 2;
        # Sleep, unless the delay is really tiny. Really tiny delays cannot
        # be accurately done, because the system calls in this loop will
        # have more overhead. The 0.0001 is arbitrary, but works fairly well.
        if ($delay / $divisor > 0.0001) {
                select(undef, undef, undef, $delay / $divisor - 0.0001);
        }

        read(TYPESCRIPT, $block, $blocksize)
                or die "read failure on typescript: $!";
        print $oldblock;
        $oldblock=$block;
}
print $oldblock;

=head1 SEE ALSO

script(1)

=head1 COPYRIGHT

This program is in the public domain.

=head1 AUTHOR

Joey Hess <joey@kitenet.net>

