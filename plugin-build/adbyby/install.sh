#!/bin/sh
# Adbyby plugin installer (called once by pluginctl after unpacking).
# Adbyby is a self-contained bundle extracted to /tmp/adbyby at boot.
set -u
PKG=$(CDPATH= cd "$(dirname "$0")" && pwd)
BASE=/etc/storage/adbyby
mkdir -p "$BASE"

# Persist the adbyby bundle and launcher.
cp -f "$PKG/adbyby.tar.gz" "$BASE/adbyby.tar.gz" 2>/dev/null || true
cp -f "$PKG/adbyby.sh" "$BASE/adbyby.sh" 2>/dev/null || true
chmod 0644 "$BASE/adbyby.tar.gz" 2>/dev/null || true
chmod 0755 "$BASE/adbyby.sh" 2>/dev/null || true

# Enable the firmware's adbyby NVRAM switch and register boot hook (idempotent).
nvram set adbyby_enable=1 2>/dev/null || true
append_hook() {
    file=$1; line=$2
    touch "$file" 2>/dev/null || return 0
    grep -Fq "$line" "$file" 2>/dev/null || echo "$line" >>"$file"
}
append_hook /etc/storage/post_wan_script.sh "[ \"\$1\" = \"up\" ] && $BASE/adbyby.sh start # ADBYBY_PLUGIN"

logger -t pluginctl "Adbyby deployed to $BASE" 2>/dev/null || true
echo "OK: Adbyby installed"
