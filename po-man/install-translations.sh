#!/usr/bin/env bash
set -eou pipefail

function usage()
{
    cat << HEREDOC

 Usage:
  $PROGRAM --mandir <directory> --mansrcdir <directory> [<man page name>...]

Install translated man pages.

 Options:
  --help                             show this help message and exit
  --mandir <mandir>                  directory in which to install the man pages, usually share/man
  --mansrcdir <mansrcdir>            directory containing the man pages to install

 Environment Variables:
  MESON_INSTALL_PREFIX               install destination prefix directory
  DESTDIR                            install destination directory

HEREDOC
}

DESTDIR="${DESTDIR:-''}"
MANPAGES=()
PROGRAM=$(basename "$0")

while [[ $# -gt 0 ]]; do
  case $1 in
    --help)
      usage
      exit 0
      ;;
    --mandir)
      MANDIR="$2"
      shift
      shift
      ;;
    --mansrcdir)
      MANSRCDIR="$2"
      shift
      shift
      ;;
    --*|-*)
      echo "Unknown option $1"
      usage
      exit 1
      ;;
    *)
      MANPAGES+=("$1")
      shift
      ;;
  esac
done

set -- "${MANPAGES[@]}"

if [ ${#MANPAGES[@]} -eq 0 ]; then
  shopt -s nullglob
  MANPAGES=("$MANSRCDIR"/*/man[0-9]/*\.[0-9])
fi

for LOCALEDIR in "$MANSRCDIR"/*/; do
    LOCALE=$(basename "$LOCALEDIR")
    for MANPAGE in "${MANPAGES[@]}"; do
        MANPAGE=$(basename "$MANPAGE")
        SECTION="${MANPAGE##*.}"
        PAGE="$LOCALEDIR/man$SECTION/$MANPAGE"
        if [ -f "$PAGE" ]; then
            if [ -z ${MESON_INSTALL_QUIET+x} ]; then
                echo "Installing $PAGE to $DESTDIR/$MESON_INSTALL_PREFIX/$MANDIR/$LOCALE/man$SECTION"
            fi
            install -D --mode=0644 --target-directory="$DESTDIR/$MESON_INSTALL_PREFIX/$MANDIR/$LOCALE/man$SECTION" "$PAGE"
        fi
    done
done
