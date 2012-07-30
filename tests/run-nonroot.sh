#!/bin/bash

MYUID=$(id -ru)
if [ $MYUID -eq 0 ]; then
	echo "The automatically executed tests suite is allowed for non-root users only."
	exit 0
fi

exec $(cd $(dirname $0) && pwd)/run.sh
