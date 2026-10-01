#!/bin/sh
# Run from any directory; the executable's relative RPATH finds bundled libraries.
set -eu
piclocate_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$piclocate_dir/bin/PicLocate" "$@"
