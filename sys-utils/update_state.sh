#!/bin/sh

if test "`whoami`" != "root"; then
    echo "This script must be executed by root"
    exit 1
fi

if test -x /usr/lib/locate/updatedb; then
    echo "WARNING: The /usr/lib/locate/find.codes file may violate the"
    echo "         privacy of your users.  Please consider making it"
    echo "         readable only by root."
    echo ""
    echo "Updating locate database"

    /usr/lib/locate/updatedb
fi

if test -d /usr/lib/texmf; then
    echo "Building ls-R cache file for TeX"
    /bin/ls -LR /usr/lib/texmf > /tmp/ls-R.$$
    if test -f /usr/lib/texmf/ls-R; then
        cp /usr/lib/texmf/ls-R /usr/lib/texmf/ls-R.old
    fi
    mv /tmp/ls-R.$$ /usr/lib/texmf/ls-R
fi

if test -x /usr/bin/makewhatis; then
    for i in /usr/man /usr/local/man /usr/X386/man /usr/interviews/man; do
        if test -d $i; then
            echo "Building whatis database in $i"
            /usr/bin/makewhatis $i
        fi
    done
fi

if test -x /usr/bin/mandb; then
    echo "Updating manpage database"
    /usr/bin/mandb
fi

exit 0
