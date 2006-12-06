#!/bin/bash
if `getopt -T >/dev/null 2>&1` ; [ $? = 4 ] ; then
  echo "Enhanced getopt(1)"
else
  echo "Old getopt(1)"
fi
