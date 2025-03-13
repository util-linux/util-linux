#!/usr/bin/env bash
set -eou pipefail

function usage()
{
    cat << HEREDOC

 Usage: $PROGRAM --srcdir <srcdir> --destdir <destdir> --asciidoctor-load-path <directory> --docdir <docdir> --po4acfg <file> --util-linux-version <version> [<asciidoc file>...]

Translate Asciidoc man page source files and generate man pages.

 Options:
  --help                               show this help message and exit
  --progress                           report the current progress
  --srcdir <srcdir>                    directory containing the asciidoc files to translate
  --destdir <destdir>                  directory in which to place the translated asciidoc files and man pages
  --asciidoctor-load-path <directory>  value for the --load-path option passed to the Asciidoctor command
  --docdir <docdir>                    directory where the package documentation will be installed
  --util-linux-version <version>       version of util-linux to include in the man pages
  --po4acfg <file>                     path to the po4a.cfg file

HEREDOC
}

PROGRAM=$(basename "$0")
PROGRESS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --srcdir)
          SRCDIR="$2"
          shift
          shift
          ;;
        --destdir)
          DESTDIR="$2"
          shift
          shift
          ;;
        --asciidoctor-load-path)
          ASCIIDOCTOR_LOAD_PATH="$2"
          shift
          shift
          ;;
        --docdir)
          DOCDIR="$2"
          shift
          shift
          ;;
        --help)
          usage
          exit 0
          ;;
        --po4acfg)
          PO4ACFG="$2"
          shift
          shift
          ;;
	--progress)
	  PROGRESS=true
	  shift
	  ;;
        --util-linux-version)
          UTIL_LINUX_VERSION="$2"
          shift
          shift
          ;;
        --*|-*)
          echo "Unknown option $1"
          usage
          exit 1
          ;;
        *)
          ADOCS+=("$1")
          shift
          ;;
    esac
done

set -- "${ADOCS[@]}"

mapfile -t LOCALES < <( awk '/\[po4a_langs\]/ {for (i=2; i<=NF; i++) print $i}' "$PO4ACFG" )
mapfile -t PO4ACFG_TRANSLATIONS < <( awk '/\[type:asciidoc\]/ {print $2;}' "$PO4ACFG" )

mkdir --parents "$DESTDIR"

DESTDIR=$( OLDPWD=- CDPATH='' cd -P -- "$DESTDIR" && pwd )

MANADOCS=()
PO4A_TRANSLATE_ONLY_FLAGS=()
for LOCALE in "${LOCALES[@]}"; do
    for ADOC in "${ADOCS[@]}"; do
	if [[ "$ADOC" == *"/man-common/manpage-stub.adoc" ]]; then
	    continue
	fi
	ADOC_NAME=$(basename "$ADOC")
        if [[ ! " ${PO4ACFG_TRANSLATIONS[*]} " =~ .*${ADOC_NAME}[[:space:]] ]]; then
	    echo "unconfigured in $PO4ACFG: $ADOC"
            continue
        fi
        PO4A_TRANSLATE_ONLY_FLAGS+=("--translate-only")
	if [[ "$ADOC" == *"/man-common/"* ]]; then
            PO4A_TRANSLATE_ONLY_FLAGS+=("$LOCALE/man-common/$ADOC_NAME")
        else
            MANADOCS+=("$LOCALE/$ADOC_NAME")
            PO4A_TRANSLATE_ONLY_FLAGS+=("$LOCALE/$ADOC_NAME")
        fi
    done
done

if [ ${#MANADOCS[@]} -eq 0 ] && [ ${#PO4A_TRANSLATE_ONLY_FLAGS[@]} -gt 0 ]; then
    echo "Only man-common Asciidoc files were supplied"
    exit 1
fi

# Only version 0.72 and later of po4a properly support the --translate-only flag.
PO4A_VERSION=$(po4a --version | { read -r _ _ v; echo "${v%*.}"; })
if echo "0.72" "$PO4A_VERSION" | sort --check --version-sort; then
  PO4A_TRANSLATE_ONLY_FLAGS=("--no-update")
fi

[ "$PROGRESS" = true ] && echo "po4a: generate man-pages translations"

DISCARDED_TRANSLATIONS=()
output=$(po4a --srcdir "$SRCDIR" --destdir "$DESTDIR" "${PO4A_TRANSLATE_ONLY_FLAGS[@]}" "$PO4ACFG")
while IFS= read -r line; do
    DISCARDED_TRANSLATION=$(echo "$line" | awk '/Discard/ {print $2;}')
    if [ -n "${DISCARDED_TRANSLATION+x}" ]; then
        DISCARDED_TRANSLATIONS+=("$DISCARDED_TRANSLATION")
    fi
done <<< "$output"

TRANSLATED_MANADOCS=()
if [ ${#MANADOCS[@]} -eq 0 ]; then
    for LOCALE in "${LOCALES[@]}"; do
        shopt -s nullglob
        TRANSLATED_MANADOCS=("$DESTDIR/$LOCALE"/*\.adoc)
    done
else
    for MANADOC in "${MANADOCS[@]}"; do
        if [[ ! " ${DISCARDED_TRANSLATIONS[*]} " =~ [[:space:]]${MANADOC}[[:space:]] ]]; then
            TRANSLATED_MANADOCS+=("$DESTDIR/$MANADOC")
        fi
    done
fi

for ADOC in "${TRANSLATED_MANADOCS[@]}"; do
    LOCALE=$(basename "$(dirname "$ADOC")")
    PAGE="${ADOC%.*}"
    SECTION="${PAGE##*.}"
    if [ "$PROGRESS" = true ]; then
        PAGENAME=$(basename $PAGE)
	echo "   GEN     " $LOCALE ": " $PAGENAME
    fi
    asciidoctor \
        --backend manpage \
        --attribute VERSION="$UTIL_LINUX_VERSION" \
        --attribute release-version="$UTIL_LINUX_VERSION" \
        --attribute ADJTIME_PATH=/etc/adjtime \
        --attribute package-docdir="$DOCDIR" \
        --base-dir "$DESTDIR/$LOCALE" \
        --destination-dir "$DESTDIR/man/$LOCALE/man$SECTION" \
        --load-path "$ASCIIDOCTOR_LOAD_PATH" \
        --require asciidoctor-includetracker \
	--require asciidoctor-unicodeconverter \
	--trace \
        "$ADOC"
done
