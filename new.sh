#!/bin/sh
set -eu

title="$1"
default_slug="$(echo "$title"| tr '[:upper:]' '[:lower:]' | sed -e 's/ /-/g' -e 's/[^a-zA-Z-]//g')"
slug="${2:-$default_slug}"
date="$(date +"%Y-%m-%d")"
root="$(git rev-parse --show-toplevel)"
filename="$root/_posts/$date-$slug.md"
relpath="$(realpath --relative-to="${PWD}" "$filename")"
# Alternatively, if on an older machine or macOS (note intentionally swapped argument order)
#   relpath="$(python3 -c "import os.path; print(os.path.relpath('$filename', '${PWD}'))")"
if ! [ -f "$relpath" ]; then
  echo "---\ntitle: \"$title\"\nlayout: post\n---\n\n" > "$relpath"
fi
echo "$relpath"
