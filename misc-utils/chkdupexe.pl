#!/usr/bin/perl
#
# chkdupexe version 2.0
#
# Simple script to look for and list duplicate executables and dangling
# symlinks in the system executable directories.
#
# Copyright 1993 Nicolai Langfeldt. Distribute under gnu copyleft
#  (included in perl package)
#
# Modified 1995-07-04 Michael Shields <shields@tembel.org>
#     Don't depend on GNU ls.
#     Cleanups.
#     Merge together $ENV{'PATH'} and $execdirs.
#     Don't break if there are duplicates in $PATH.
# 

$execdirs='/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin:/local/bin:/local/sbin:/usr/X11/bin:/usr/bin/X11:/usr/local/X11/bin:/local/X11/bin:/usr/TeX/bin:/usr/tex/bin:/usr/local/graph/bin:/usr/games:/usr/local/games:/usr/intervies/bin/LINUX';

DIRECTORY:
foreach $dir (split(/:/, "$execdirs:$ENV{'PATH'}")) {

  # Follow symlinks and make sure we haven't scanned this directory already.
  while (-l $dir) {
    $newdir = readlink($dir);
    print "Dangling symlink: $dir\n" unless $newdir;
    $dir = $newdir;
    next DIRECTORY if $seendir{$dir}++;
  }

  opendir(DIR,$dir) || (warn "Couldn't opendir($dir): $!\n", next);
  foreach $_ (readdir(DIR)) {
    lstat("$dir/$_");
    if (-l _) {
      ($dum)=stat("$dir/$_");
      # Might as well report these since we discover them anyway
      print "Dangling symlink: $dir/$_\n" unless $dum;
      next;
    }
    next unless -f _ && -x _;	# Only handle regular executable files
    if ($count{$_}) {
      $progs{$_}.=" $dir/$_";
      $count{$_}++;
    } else {
      $progs{$_}="$dir/$_";
      $count{$_}=1;
    }
  }
  closedir(DIR);
}

open(LS,"| xargs ls -ldU");
while (($prog,$paths)=each %progs) {
  print LS "$paths\n" if ($count{$prog}>1);
}
close(LS);
