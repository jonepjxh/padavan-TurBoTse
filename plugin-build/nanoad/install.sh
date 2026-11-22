#!/bin/sh
# NanoAd plugin installer (called once by pluginctl after unpacking to <plugin dir>).
# Deploys a self-contained NanoAd runtime to /etc/storage/nanoad (persisted) and
# registers boot + firewall hooks. WebUI needs no login.
set -u
PKG=$(CDPATH= cd "$(dirname "$0")" && pwd)
BASE=/etc/storage/nanoad
umask 077
mkdir -p "$BASE/web" /tmp/nanoad

# Deploy binaries and runtime files from the plugin payload.
for f in nanoad nanoad-ctl nanoad-update; do
    cp -f "$PKG/usr/bin/$f" "$BASE/$f" 2>/dev/null || cp -f "$PKG/$f" "$BASE/$f" 2>/dev/null || true
done
[ -f "$PKG/nanoad.conf" ] && cp -f "$PKG/nanoad.conf" "$BASE/nanoad.conf"
[ -f "$PKG/rules.compact.txt.gz" ] && cp -f "$PKG/rules.compact.txt.gz" "$BASE/rules.compact.txt.gz"
for f in subscriptions.txt custom-allow.txt custom-block.txt; do
    [ -f "$PKG/$f" ] && cp -f "$PKG/$f" "$BASE/$f"
done
[ -f "$PKG/web/index.html" ] && cp -f "$PKG/web/index.html" "$BASE/web/index.html"

chmod 0755 "$BASE/nanoad" "$BASE/nanoad-ctl" "$BASE/nanoad-update" 2>/dev/null || true
chmod 0644 "$BASE/web/index.html" 2>/dev/null || true
chmod 0600 "$BASE/nanoad.conf" 2>/dev/null || true

# Point WebUI root at the persisted storage copy.
if [ -f "$BASE/nanoad.conf" ]; then
    sed "s#^web_root=.*#web_root=$BASE/web#" "$BASE/nanoad.conf" >"$BASE/nanoad.conf.tmp" \
        && mv -f "$BASE/nanoad.conf.tmp" "$BASE/nanoad.conf"
fi

# Register boot hook (idempotent) and firewall hook.
append_hook() {
    file=$1; line=$2
    touch "$file" 2>/dev/null || return 0
    grep -Fq "$line" "$file" 2>/dev/null || echo "$line" >>"$file"
}
append_hook /etc/storage/post_iptables_script.sh "$BASE/nanoad-ctl firewall # NANOAD_PLUGIN"
if [ -f /etc/storage/post_wan_script.sh ]; then
    append_hook /etc/storage/post_wan_script.sh "[ \"\$1\" = \"up\" ] && $BASE/nanoad-ctl boot # NANOAD_PLUGIN"
else
    append_hook /etc/storage/post_start.sh "$BASE/nanoad-ctl boot # NANOAD_PLUGIN"
fi

logger -t pluginctl "NanoAd deployed to $BASE" 2>/dev/null || true
echo "OK: NanoAd installed (no login required)"
