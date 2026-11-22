#!/bin/sh
# Padavan 4.4 "Advanced -> Ad management" launcher.
# The original adbyby package slot is reused to ship NanoAd inside the firmware.
# Called by /usr/sbin/rc via start_adbyby()/stop_adbyby()/update_adb().
set -u

BASE=/etc/storage/nanoad
RO=/etc_ro/nanoad
CTL="$BASE/nanoad-ctl"

prepare() {
    mkdir -p "$BASE/web" /tmp/nanoad
    [ -f "$BASE/nanoad" ] || cp -f /usr/bin/nanoad "$BASE/nanoad"
    [ -f "$BASE/nanoad-ctl" ] || cp -f /usr/bin/nanoad-ctl "$BASE/nanoad-ctl"
    [ -f "$BASE/nanoad-update" ] || cp -f /usr/bin/nanoad-update "$BASE/nanoad-update"
    [ -f "$BASE/nanoad.conf" ] || cp -f "$RO/nanoad.conf" "$BASE/nanoad.conf"
    [ -f "$BASE/rules.compact.txt.gz" ] || cp -f "$RO/rules.compact.txt.gz" "$BASE/rules.compact.txt.gz"
    for f in subscriptions.txt custom-allow.txt custom-block.txt; do
        [ -f "$BASE/$f" ] || cp -f "$RO/$f" "$BASE/$f"
    done
    [ -f "$BASE/web/index.html" ] || cp -f "$RO/web/index.html" "$BASE/web/index.html"
    chmod 0755 "$BASE/nanoad" "$BASE/nanoad-ctl" "$BASE/nanoad-update" 2>/dev/null || true
    chmod 0644 "$BASE/web/index.html" 2>/dev/null || true
    chmod 0600 "$BASE/nanoad.conf" 2>/dev/null || true
    # Keep the bundled WebUI root pointing at the storage copy.
    if [ -f "$BASE/nanoad.conf" ]; then
        sed "s#^web_root=.*#web_root=$BASE/web#" "$BASE/nanoad.conf" >"$BASE/nanoad.conf.tmp" \
            && mv -f "$BASE/nanoad.conf.tmp" "$BASE/nanoad.conf"
    fi
}

case "${1:-}" in
    start)
        prepare
        "$CTL" boot >/dev/null 2>&1
        logger -t NanoAd "NanoAd started from firmware" 2>/dev/null || true
        ;;
    stop)
        [ -x "$CTL" ] && "$CTL" stop >/dev/null 2>&1
        logger -t NanoAd "NanoAd stopped" 2>/dev/null || true
        ;;
    updateadb|update)
        prepare
        "$BASE/nanoad-update" >/dev/null 2>&1
        "$CTL" restart >/dev/null 2>&1
        ;;
    restart)
        prepare
        "$CTL" restart >/dev/null 2>&1
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|updateadb}" >&2
        exit 1
        ;;
esac
