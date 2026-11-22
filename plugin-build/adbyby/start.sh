#!/bin/sh
# pluginctl start / autostart entry for Adbyby.
BASE=/etc/storage/adbyby
# Make sure the firmware's read-only slot can find the bundle by symlinking into /etc_ro path is
# not possible; call the launcher directly after extracting our persisted copy.
if [ ! -x /tmp/adbyby/adbyby ]; then
    mkdir -p /tmp
    tar -xzf "$BASE/adbyby.tar.gz" -C /tmp >/dev/null 2>&1
fi
if [ -x "$BASE/adbyby.sh" ]; then
    # Point the launcher at our persisted bundle instead of /etc_ro.
    sed "s#/etc_ro/adbyby.tar.gz#$BASE/adbyby.tar.gz#g" "$BASE/adbyby.sh" >/tmp/adbyby-launch.sh 2>/dev/null
    chmod 0755 /tmp/adbyby-launch.sh 2>/dev/null
    /tmp/adbyby-launch.sh start >/dev/null 2>&1
else
    /tmp/adbyby/adbyby >/dev/null 2>&1 &
fi
echo "OK: Adbyby started"
