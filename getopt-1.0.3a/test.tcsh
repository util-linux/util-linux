#!/bin/tcsh
getopt -T >&/dev/null
if ( $status == 4) then
  echo "Enhanced getopt(1)"
else
  echo "Old getopt(1)"
endif
