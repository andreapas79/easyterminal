#!/bin/sh
# Build easyterminal.
#   ./build.sh        build tree in ./build (run it with ./run-dev.sh)
#   ./build.sh deb    also build ../easyterminal_*.deb (not installed)
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here"

# Build dependencies (only -dev packages and build tools; nothing that
# touches the system gnome-terminal). Installing them needs sudo.
if ! (cd "$src" && dpkg-checkbuilddeps 2>/dev/null); then
  echo "Missing build dependencies:" >&2
  (cd "$src" && dpkg-checkbuilddeps) || true
  echo "Install them with: sudo apt install <packages above>" >&2
  exit 1
fi

if [ ! -d "$here/build" ]; then
  meson setup "$here/build" "$src" \
    -Ddocs=false -Dnautilus_extension=false -Dsearch_provider=false
fi
ninja -C "$here/build"

if [ "$1" = deb ]; then
  (cd "$src" && dpkg-buildpackage -b -us -uc)
  # dpkg-buildpackage leaves the .deb next to the source directory
  ls -l "$here"/../easyterminal_*.deb
fi
