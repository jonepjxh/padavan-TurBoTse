#!/bin/sh
# pluginctl stop entry for NanoAd.
BASE=/etc/storage/nanoad
[ -x "$BASE/nanoad-ctl" ] || exit 0
"$BASE/nanoad-ctl" stop >/dev/null 2>&1
echo "OK: NanoAd stopped"
