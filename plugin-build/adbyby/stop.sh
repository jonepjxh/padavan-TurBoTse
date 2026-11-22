#!/bin/sh
# pluginctl stop entry for Adbyby.
if [ -x /etc/storage/adbyby/adbyby.sh ]; then
    sed "s#/etc_ro/adbyby.tar.gz#/etc/storage/adbyby/adbyby.tar.gz#g" /etc/storage/adbyby/adbyby.sh >/tmp/adbyby-stop.sh 2>/dev/null
    chmod 0755 /tmp/adbyby-stop.sh 2>/dev/null
    /tmp/adbyby-stop.sh stop >/dev/null 2>&1
fi
killall -q adbyby 2>/dev/null
echo "OK: Adbyby stopped"
