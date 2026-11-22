#!/usr/bin/env python3
# Build the nanoad and adbyby standalone plugin packages and append them to
# the plugin repository index. Runs on the CI runner AFTER the firmware build
# (so the cross toolchain and romfs are available).
#
# Environment / paths (relative to CWD = src/):
#   toolchain/mipsel-linux-musl/bin/mipsel-linux-musl-gcc   cross compiler
#   toolchain/mipsel-linux-musl/bin/mipsel-linux-musl-strip strip
#   nanoad-src/                         extracted NanoAd source (trunk/user/adbyby)
#   trunk/romfs/etc_ro/adbyby.tar.gz    original adbyby bundle (from firmware build)
#   ../plugin_patch/                    install/start/stop scripts for both plugins
#   plugin-repo/                        output dir (existing 27 plugins + index)
import os, sys, json, hashlib, tarfile, shutil, subprocess

SRC = os.getcwd()
REPO = os.path.join(SRC, 'plugin-repo')
os.makedirs(REPO, exist_ok=True)
PATCH = os.environ.get('PLUGIN_PATCH_DIR', os.path.join(SRC, '..', 'plugin_patch'))
NA_SRC = os.environ.get('NANOAD_SRC_DIR', os.path.join(SRC, 'nanoad-src'))
CC = os.environ.get('CROSS_CC', os.path.join(SRC, 'toolchain', 'mipsel-linux-musl', 'bin', 'mipsel-linux-musl-gcc'))
STRIP = os.environ.get('CROSS_STRIP', os.path.join(SRC, 'toolchain', 'mipsel-linux-musl', 'bin', 'mipsel-linux-musl-strip'))

FANCY = {'nanoad': 'NanoAd 广告过滤', 'adbyby': 'Adbyby 广告过滤'}
errors = []


def add_file(tf, path, arcname):
    tf.add(path, arcname)


def sh(src, dst):
    shutil.copy2(src, dst)


def make_nanoad():
    """Compile NanoAd and assemble its package."""
    print('=== building nanoad plugin ===', flush=True)
    src_dir = NA_SRC
    c_sources = [os.path.join(src_dir, 'src', f) for f in
                 ('main.c', 'util.c', 'rules.c', 'dns.c', 'sni.c', 'web.c')]
    for f in c_sources:
        if not os.path.isfile(f):
            errors.append('nanoad source missing: ' + f)
            return None
    binp = os.path.join(src_dir, 'nanoad')
    cmd = [CC, '-std=c99', '-Os', '-g0', '-Wall', '-Wextra', '-Wshadow', '-Wformat=2',
           '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections',
           '-D_FILE_OFFSET_BITS=64', '-D_GNU_SOURCE', '-static', '-Wl,--gc-sections',
           '-o', binp] + c_sources
    print('CC:', ' '.join(cmd), flush=True)
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(r.stdout, flush=True)
    if r.returncode != 0:
        errors.append('nanoad compile failed')
        return None
    if os.path.isfile(STRIP):
        subprocess.run([STRIP, '--strip-all', binp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.isfile(binp) or os.path.getsize(binp) < 1000:
        errors.append('nanoad binary missing/too small')
        return None

    tmpd = os.path.join(SRC, '_pk_nanoad')
    if os.path.isdir(tmpd):
        shutil.rmtree(tmpd)
    os.makedirs(os.path.join(tmpd, 'web'), exist_ok=True)
    # runtime files
    sh(binp, os.path.join(tmpd, 'nanoad'))
    for s in ('nanoad-ctl', 'nanoad-update'):
        p = os.path.join(src_dir, 'scripts', s)
        if os.path.isfile(p):
            sh(p, os.path.join(tmpd, s))
        else:
            errors.append('nanoad script missing: ' + s)
    cfgdir = os.path.join(src_dir, 'config')
    for s in ('nanoad.conf', 'subscriptions.txt', 'custom-allow.txt', 'custom-block.txt', 'rules.compact.txt.gz'):
        p = os.path.join(cfgdir, s)
        if os.path.isfile(p):
            sh(p, os.path.join(tmpd, s))
    web = os.path.join(src_dir, 'web', 'index.html')
    if os.path.isfile(web):
        sh(web, os.path.join(tmpd, 'web', 'index.html'))
    for s in ('install.sh', 'start.sh', 'stop.sh'):
        p = os.path.join(PATCH, 'nanoad', s)
        if os.path.isfile(p):
            sh(p, os.path.join(tmpd, s))
        else:
            errors.append('nanoad patch script missing: ' + s)
    for s in ('nanoad', 'nanoad-ctl', 'nanoad-update', 'install.sh', 'start.sh', 'stop.sh'):
        os.chmod(os.path.join(tmpd, s), 0o755)
    # manifest
    files = []
    for dp, _, fs in os.walk(tmpd):
        for f in fs:
            files.append(os.path.relpath(os.path.join(dp, f), tmpd).replace('\\', '/'))
    files.sort()
    with open(os.path.join(tmpd, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump({'name': 'nanoad', 'title': FANCY['nanoad'], 'arch': 'mipsel-linux-musl',
                   'files': files}, f, ensure_ascii=False)
    out = os.path.join(REPO, 'nanoad.tar.gz')
    with tarfile.open(out, 'w:gz') as tf:
        for dp, _, fs in os.walk(tmpd):
            for f in fs:
                full = os.path.join(dp, f)
                add_file(tf, full, os.path.relpath(full, tmpd).replace('\\', '/'))
    print('PACKED nanoad', os.path.getsize(out), 'bytes', flush=True)
    return out


def make_adbyby():
    print('=== building adbyby plugin ===', flush=True)
    bundle = os.path.join(SRC, 'trunk', 'romfs', 'etc_ro', 'adbyby.tar.gz')
    if not os.path.isfile(bundle):
        # fallback: upstream prebuilt bundle
        bundle = os.path.join(SRC, 'adbyby.tar.gz')
    if not os.path.isfile(bundle):
        errors.append('adbyby.tar.gz not found in romfs')
        return None
    tmpd = os.path.join(SRC, '_pk_adbyby')
    if os.path.isdir(tmpd):
        shutil.rmtree(tmpd)
    os.makedirs(tmpd, exist_ok=True)
    sh(bundle, os.path.join(tmpd, 'adbyby.tar.gz'))
    # launcher script from romfs if present, else minimal
    launcher = os.path.join(SRC, 'trunk', 'romfs', 'usr', 'bin', 'adbyby.sh')
    if os.path.isfile(launcher):
        sh(launcher, os.path.join(tmpd, 'adbyby.sh'))
    else:
        open(os.path.join(tmpd, 'adbyby.sh'), 'w').write('#!/bin/sh\ntar -xzf /etc_ro/adbyby.tar.gz -C /tmp 2>/dev/null\n/tmp/adbyby/adbyby >/dev/null 2>&1 &\n')
    for s in ('install.sh', 'start.sh', 'stop.sh'):
        p = os.path.join(PATCH, 'adbyby', s)
        if os.path.isfile(p):
            sh(p, os.path.join(tmpd, s))
        else:
            errors.append('adbyby patch script missing: ' + s)
    for s in ('install.sh', 'start.sh', 'stop.sh', 'adbyby.sh'):
        os.chmod(os.path.join(tmpd, s), 0o755)
    files = []
    for dp, _, fs in os.walk(tmpd):
        for f in fs:
            files.append(os.path.relpath(os.path.join(dp, f), tmpd).replace('\\', '/'))
    files.sort()
    with open(os.path.join(tmpd, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump({'name': 'adbyby', 'title': FANCY['adbyby'], 'arch': 'mipsel-linux-musl',
                   'files': files}, f, ensure_ascii=False)
    out = os.path.join(REPO, 'adbyby.tar.gz')
    with tarfile.open(out, 'w:gz') as tf:
        for dp, _, fs in os.walk(tmpd):
            for f in fs:
                full = os.path.join(dp, f)
                add_file(tf, full, os.path.relpath(full, tmpd).replace('\\', '/'))
    print('PACKED adbyby', os.path.getsize(out), 'bytes', flush=True)
    return out


def append_index(paths):
    """Append entries to plugins.json and plugins.txt, dedup by name."""
    name_to_entry = {}
    for name, out in paths.items():
        if not out:
            continue
        sha = hashlib.sha256(open(out, 'rb').read()).hexdigest()
        name_to_entry[name] = {
            'name': name, 'title': FANCY[name], 'file': name + '.tar.gz',
            'size': os.path.getsize(out), 'sha256': sha, 'files': [],
        }
    # plugins.json
    jpath = os.path.join(REPO, 'plugins.json')
    data = json.load(open(jpath, encoding='utf-8')) if os.path.isfile(jpath) else {'arch': 'mipsel-linux-musl', 'build': '', 'plugins': []}
    existing = {p['name']: p for p in data['plugins']}
    for name, e in name_to_entry.items():
        # fill files from manifest
        mp = os.path.join(REPO, name + '.tar.gz')
        try:
            with tarfile.open(mp, 'r:gz') as tf:
                e['files'] = [m.name for m in tf.getmembers() if m.isfile() and m.name != 'manifest.json']
        except Exception:
            pass
        existing[name] = e
    data['plugins'] = list(existing.values())
    with open(jpath, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    # plugins.txt (TSV: name|title|file|size|sha256)
    tpath = os.path.join(REPO, 'plugins.txt')
    lines = []
    seen = set()
    if os.path.isfile(tpath):
        for line in open(tpath, encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or '|' not in line:
                continue
            n = line.split('|', 1)[0]
            if n in name_to_entry or n in seen:
                continue
            seen.add(n)
            lines.append(line)
    for name, e in name_to_entry.items():
        lines.append('%s|%s|%s|%s|%s' % (e['name'], e['title'], e['file'], e['size'], e['sha256']))
    with open(tpath, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('INDEX total plugins:', len(lines), flush=True)


def main():
    paths = {}
    p = make_nanoad()
    if p:
        paths['nanoad'] = p
    p = make_adbyby()
    if p:
        paths['adbyby'] = p
    append_index(paths)
    if errors:
        print('ERRORS:', errors, flush=True)
        sys.exit(1)
    print('DONE extra plugins', flush=True)


if __name__ == '__main__':
    main()
