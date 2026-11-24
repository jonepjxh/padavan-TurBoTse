# -*- coding: utf-8 -*-
"""inject.py - inject webui.conf + start/stop scripts into every packaged plugin.

Runs inside CI after plugin packages (incl. nanoad/adbyby) are built.
Source payload is /tmp/plugin-webui/<pkg>/{webui.conf,start.sh,stop.sh}
(produced locally from specs_* and shipped as plugin-build/plugin-webui.tar.gz).
"""
import os, json, hashlib, tarfile, shutil, stat

WEBROOT = os.environ.get("WEBUI_ROOT", "/tmp/plugin-webui")
WRAP = ("nanoad", "adbyby")   # keep their own start.sh, merge config first

WRAP_TMPL = """#!/bin/sh
# {pkg} - start wrapper: merge plugin-center settings into the plugin's own conf, then run original start
B=/etc/storage/plugins/{pkg}
C=$B/config
CF=$B/nanoad.conf
if [ -s "$C" ]; then
	while IFS= read -r line; do
		case "$line" in
			''|'#'*) continue ;;
			*=*) ;;
			*) continue ;;
		esac
		k=${{line%%=*}}
		if [ -f "$CF" ]; then
			grep -v "^$k=" "$CF" > /tmp/pc_nc.$$ 2>/dev/null
			{{ cat /tmp/pc_nc.$$ 2>/dev/null; echo "$line"; }} > "$CF" 2>/dev/null
			rm -f /tmp/pc_nc.$$
		elif [ -d "$B" ]; then
			echo "$line" >> "$CF" 2>/dev/null
		fi
	done < "$C"
fi
[ -x "$B/start.orig.sh" ] && exec sh "$B/start.orig.sh"
exit 0
"""


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def chmodx(p, mode=0o755):
    os.chmod(p, mode)


def main():
    root = os.getcwd()
    pkgdir = os.path.join(root, "plugin-repo")
    jf = os.path.join(pkgdir, "plugins.json")
    if not os.path.isfile(jf):
        print("ERROR: plugins.json not found at", jf)
        raise SystemExit(1)
    data = json.load(open(jf, encoding="utf-8"))
    if isinstance(data, dict):
        entries = data["plugins"]
    else:
        entries = data
    injected = missed = 0
    for e in entries:
        name = e["name"]
        src = os.path.join(WEBROOT, name)
        tarpath = os.path.join(pkgdir, e["file"])
        if not os.path.isdir(src) or not os.path.isfile(tarpath):
            print("SKIP (no payload or package):", name)
            missed += 1
            continue
        tmpd = os.path.join(os.environ.get("INJECT_TMP", "/tmp"), "inj_" + name)
        if os.path.isdir(tmpd):
            shutil.rmtree(tmpd)
        os.makedirs(tmpd, exist_ok=True)
        with tarfile.open(tarpath, "r:gz") as tf:
            tf.extractall(tmpd)

        added = []
        # 1) webui.conf (settings form data for the generic admin page)
        wc = os.path.join(src, "webui.conf")
        if os.path.isfile(wc):
            dst = os.path.join(tmpd, "webui.conf")
            shutil.copyfile(wc, dst)
            chmodx(dst, 0o644)
            added.append("webui.conf")
        # 2) service scripts
        if name in WRAP:
            # keep the vendor start.sh as start.orig.sh, ours merges config first;
            # the vendor stop.sh is left untouched.
            dst = os.path.join(tmpd, "start.sh")
            ori = os.path.join(tmpd, "start.orig.sh")
            if os.path.isfile(dst) and not os.path.isfile(ori):
                shutil.copyfile(dst, ori)
                chmodx(ori, 0o755)
            with open(dst, "w", encoding="utf-8", newline="\n") as f:
                f.write(WRAP_TMPL.format(pkg=name))
            chmodx(dst, 0o755)
            added.append("start.sh(wrap)+start.orig.sh")
        else:
            for fn in ("start.sh", "stop.sh"):
                sp = os.path.join(src, fn)
                if not os.path.isfile(sp):
                    continue
                dst = os.path.join(tmpd, fn)
                shutil.copyfile(sp, dst)
                chmodx(dst, 0o755)
                added.append(fn)
        # 3) manifest: record extra files so the installer/index stays truthful
        mp = os.path.join(tmpd, "manifest.json")
        try:
            man = json.load(open(mp, encoding="utf-8"))
        except Exception:
            man = {"name": name, "title": name, "arch": "mipsel-linux-musl", "files": []}
        for a in ("webui.conf", "start.sh", "stop.sh", "start.orig.sh", "stop.orig.sh"):
            if os.path.isfile(os.path.join(tmpd, a)) and a not in man.get("files", []):
                man.setdefault("files", []).append(a)
        with open(mp, "w", encoding="utf-8") as f:
            json.dump(man, f, ensure_ascii=False)

        with tarfile.open(tarpath, "w:gz") as tf:
            for dp, _, fs in os.walk(tmpd):
                for f in sorted(fs):
                    full = os.path.join(dp, f)
                    tf.add(full, os.path.relpath(full, tmpd))
        e["size"] = os.path.getsize(tarpath)
        e["sha256"] = sha(tarpath)
        e.pop("files", None)
        injected += 1
        print("INJECTED", name, "->", ",".join(added))

    with open(jf, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    with open(os.path.join(pkgdir, "plugins.txt"), "w", encoding="utf-8") as f:
        for p in entries:
            f.write("%s|%s|%s|%s|%s\n" % (p["name"], p["title"], p["file"], p["size"], p["sha256"]))
    print("DONE injected=%d skipped=%d total=%d" % (injected, missed, len(entries)))


if __name__ == "__main__":
    main()


