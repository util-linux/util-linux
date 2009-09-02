#!/bin/sh

VERSION="0.3"
OUT="$1"

if head=`git rev-parse --verify HEAD 2>/dev/null`; then
	git update-index --refresh --unmerged > /dev/null
	descr=$(git describe 2>/dev/null || echo "v$VERSION")

	# on git builds check that the version number above
	# is correct...
	[ "${descr%%-*}" = "v$VERSION" ] || exit 2

	echo -n 'const char rfkill_version[] = "' > "$OUT"
	v="${descr#v}"
	if git diff-index --name-only HEAD | read dummy ; then
		v="$v"-dirty
	fi
else
	v="$VERSION"
fi

echo "const char rfkill_version[] = \"$v\";" > "$OUT"
