#!/bin/sh
# Run easyterminal from the build tree, without installing anything.
# Settings live in memory only (GSETTINGS_BACKEND=memory): nothing is
# written to dconf, neither for gnome-terminal nor for easyterminal.
set -e
here=$(cd "$(dirname "$0")" && pwd)
build="$here/build"
export GSETTINGS_SCHEMA_DIR="$build/src"
export GSETTINGS_BACKEND=memory
# Don't inherit the system gnome-terminal / a previous session's env
unset EASYTERMINAL_SCREEN EASYTERMINAL_SERVICE

if ! gdbus call --session --dest org.freedesktop.DBus \
     --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.NameHasOwner \
     io.github.andreapas79.easyterminal | grep -q true; then
  "$build/src/easyterminal-server" &
  # wait until the server owns its bus name
  for i in $(seq 50); do
    gdbus call --session --dest org.freedesktop.DBus \
      --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.NameHasOwner \
      io.github.andreapas79.easyterminal | grep -q true && break
    sleep 0.1
  done
fi
exec "$build/src/easyterminal" "$@"
