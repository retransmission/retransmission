#!/bin/sh

# Compiles each language's catalog into the Mac app's bundle,
# along with the .strings files that AppKit translates the xibs with.
# Both builds of the app run this once the bundle exists:
#   compile-mac-catalogs.sh <the bundle's Resources folder>
# AppKit picks the app's language from the <language>.lproj folders that these go into.
# MSGFMT and PYTHON3 name the tools to use; without them the script looks for msgfmt and python3.
# An empty MSGFMT builds the app in English.
#
# Python is required, msgfmt is not: the xibs' text marks mnemonics for the other clients,
# and only a .strings file shows it without the markers, in English as in any other language.

set -e

RESOURCES="$1"
cd "$(dirname "$0")/.."

# Xcode builds with a PATH that leaves out Homebrew and MacPorts.
PATH="$PATH:/opt/homebrew/opt/gettext/bin:/usr/local/opt/gettext/bin:/opt/local/bin"
MSGFMT="${MSGFMT-msgfmt}"
PYTHON3="${PYTHON3:-python3}"

if ! command -v "$PYTHON3" > /dev/null; then
  echo "error: python3 was not found. The app's text needs it. Xcode's command line tools provide it."
  exit 1
fi

if [ -z "$MSGFMT" ] || ! command -v "$MSGFMT" > /dev/null; then
  echo "warning: gettext's msgfmt was not found, so the app's text will be in English. To fix this, run: brew install gettext"
  MSGFMT=
fi

for LPROJ in macosx/*.lproj; do
  LANGUAGE=$(basename "$LPROJ" .lproj)
  PO="po/$(echo "$LANGUAGE" | tr - _).po"
  if [ "$LANGUAGE" = Base ]; then
    continue
  elif [ -n "$MSGFMT" ] && [ -f "$PO" ]; then
    mkdir -p "$RESOURCES/$LANGUAGE.lproj"
    # The app looks for a catalog named after the gettext domain, which is the app's name.
    "$MSGFMT" --output-file="$RESOURCES/$LANGUAGE.lproj/retransmission.mo" "$PO"
    "$PYTHON3" release/mac-xib-strings.py strings "$PO" "$RESOURCES/$LANGUAGE.lproj" macosx/Base.lproj/*.xib
  elif [ "$LANGUAGE" = en ] || [ -d "$RESOURCES/$LANGUAGE.lproj" ]; then
    # English, or a language that the bundle has a folder for but no catalog: the xibs' own text, without its markers.
    # No folder is made for such a language, since AppKit would run the app in it.
    mkdir -p "$RESOURCES/$LANGUAGE.lproj"
    "$PYTHON3" release/mac-xib-strings.py strings - "$RESOURCES/$LANGUAGE.lproj" macosx/Base.lproj/*.xib
  fi
done
