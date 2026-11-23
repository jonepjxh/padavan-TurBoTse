#!/bin/sh
# zzdemo - plugin center demo plugin.
# The only thing this plugin needs is the web/ directory: after install the
# page shows up under Advanced Settings automatically (see README).
DEST=$(cd "$(dirname "$0")" && pwd)
{
    printf 'installed at %s\n' "$DEST"
    printf 'menu.conf: %s\n' "$(cat "$DEST/web/menu.conf" 2>/dev/null)"
} > "$DEST/info.txt" 2>/dev/null
echo "zzdemo installed"
exit 0
