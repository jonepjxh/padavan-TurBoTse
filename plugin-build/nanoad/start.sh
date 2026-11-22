#!/bin/sh
# pluginctl start / autostart entry for NanoAd.
BASE=/etc/storage/nanoad
[ -x "$BASE/nanoad-ctl" ] || { echo "ERR: NanoAd not installed"; exit 1; }
"$BASE/nanoad-ctl" boot >/dev/null 2>&1 &
echo "OK: NanoAd started"
