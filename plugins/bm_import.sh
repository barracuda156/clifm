#!/bin/sh

# Clifm plugin to import bookmarks from either Ranger or Midnight Commander
# Description: Import Ranger/MC bookmarks from FILE
# Author: L. Abramovich
# License: GPL2+

if [ -z "$1" ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
	name="${CLIFM_PLUGIN_NAME:-$(basename "$0")}"
	printf "Import Ranger or Midnight Commander bookmarks from FILE

\x1b[1mUSAGE\x1b[0m\n  %s FILE\n" "$name"
	exit 0
fi

file="$(echo "$1" | sed 's/\\//g')"

if ! [ -f "$file" ]; then
	printf "clifm: %s: No such file or directory\n" "$file" >&2
	exit 1
fi

if [ -z "$CLIFM" ] || ! [ -f "${CLIFM}/bookmarks.clifm" ]; then
	printf "Bookmarks file for Clifm not found\n" >&2
	exit 1
fi

clifm_bm="${CLIFM}/bookmarks.clifm"
bmn=0

if grep -q ^"ENTRY " "$file"; then
	fm="mc"
else
	fm="ranger"
fi

while read -r line; do
	if [ "$fm" = "mc" ]; then
		name="$(echo "$line" | awk '{print $2}' | sed 's/\"//g')"
		path="$(echo "$line" | awk '{print $4}' | sed 's/\"//g')"
	else
		name="$(echo "$line" | cut -d':' -f1 2>/dev/null)"
		path="$(echo "$line" | cut -d':' -f2 2>/dev/null)"
	fi

	if [ -z "$name" ] || [ -z "$path" ]; then
		printf "clifm: %s: Bookmark cannot be imported\n" "$line" >&2
		continue
	fi
	bmn=$((bmn + 1))
	printf "[%s]%s\n" "$name" "$path" >> "$clifm_bm"
done < "$file"

if [ "$bmn" -gt 0 ]; then
	printf "clifm: %d bookmark(s) successfully imported\n" "$bmn"
	if [ -n "$CLIFM_BUS" ]; then
		echo "bm reload" > "$CLIFM_BUS"
	else
		printf "Restart CliFM for changes to take effect\n"
	fi
else
	printf "clifm: No bookmarks imported\n"
fi

exit 0
