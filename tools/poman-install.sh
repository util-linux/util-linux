#!/usr/bin/env bash
set -eou pipefail

function usage()
{
    cat << HEREDOC

 Usage:
  $PROGRAM --install|--uninstall --mandir <directory> --mansrcdir <directory> [<man page name>...]

Install translated man pages.

 Options:
  --help                             show this help message and exit
  --mandir <mandir>                  directory in which to install the man pages, usually share/man
  --mansrcdir <mansrcdir>            directory containing the man pages to install

 Environment Variables:
  MESON_INSTALL_PREFIX               install destination prefix directory

HEREDOC
}

MANPAGES=()
PROGRAM=$(basename "$0")
MYCMD="install"

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
    --install)
      MYCMD="install"
      shift
      ;;
    --uninstall)
      MYCMD="uninstall"
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
     
    if [ "$MYCMD" = "install" ]; then
      PAGE="$LOCALEDIR/man$SECTION/$MANPAGE"
      if [ -f "$PAGE" ]; then
        if [ -z ${MESON_INSTALL_QUIET+x} ]; then
	  echo "Installing $PAGE to ${MANDIR}/$LOCALE/man$SECTION"
	fi
	mkdir -p "${MANDIR}/$LOCALE/man$SECTION"
        install -m 644 "$PAGE" "${MANDIR}/$LOCALE/man$SECTION"
      fi

    elif [ "$MYCMD" = "uninstall" ]; then
      if [ -z ${MESON_INSTALL_QUIET+x} ]; then
        echo "Uninstalling ${MANDIR}/$LOCALE/man$SECTION/$MANPAGE"
      fi
      rm -f "${MANDIR}/$LOCALE/man$SECTION/$MANPAGE"
    fi

  done
done
