#!/bin/bash
# Build Padavan (mipsel-linux-musl) plugin packages from upstream GitHub sources,
# then publish them to the `plugin-repo` release and refresh plugins.txt.
#
# Runs on GitHub Actions (ubuntu) or any x86_64 Linux with go + upx + gh.
#   PUB=0  -> only build, keep artifacts in plugins/.out
#   PUB=1  -> also upload + update plugins.txt (needs GH_TOKEN)
set -u

REPO="${REPO:-jonepjxh/padavan-TurBoTse}"
TAG="${TAG:-plugin-repo}"
PUB="${PUB:-0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/plugins/.work"
OUT="$ROOT/plugins/.out"
ST="$WORK/stage"
rm -rf "$WORK"; mkdir -p "$WORK" "$OUT" "$ST"

export CGO_ENABLED=0 GOOS=linux GOARCH=mipsle GOMIPS=softfloat GOFLAGS=-mod=mod
export GOTOOLCHAIN=auto

log(){ echo "[build] $*"; }

# 上游源码（浅克隆）到 $WORK/src/<name>，返回版本串
fetch_src(){ # name repo
	local n="$1" r="$2"
	if [ ! -d "$WORK/src/$n" ]; then
		git clone --depth 1 "https://github.com/$r.git" "$WORK/src/$n" >/dev/null 2>&1 || return 1
	fi
	( cd "$WORK/src/$n" && git describe --tags --always 2>/dev/null | head -1 )
}

# 交叉编译：name pkgpath(bin 名同 name)
gobuild(){ # name pkgpath outbin
	local n="$1" p="$2" o="$3"
	( cd "$WORK/src/$n" && go build -trimpath -ldflags "-s -w" -o "$WORK/bin/$n" "$p" ) >"$WORK/$n.build.log" 2>&1 || {
		log "BUILD FAIL $n"; tail -5 "$WORK/$n.build.log"; return 1; }
	[ -f "$WORK/bin/$n" ] || return 1
	if command -v upx >/dev/null 2>&1; then
		upx -9 -q "$WORK/bin/$n" >/dev/null 2>&1 || true
	fi
	cp "$WORK/bin/$n" "$o"
	log "built $n $(stat -c%s "$o") bytes"
}
mkdir -p "$WORK/bin"

# 组装包目录：name title cat
stage(){ # name title cat
	local n="$1" t="$2" c="$3"
	P="$ST/$n"
	mkdir -p "$P/usr/bin"
	cat > "$P/manifest.json" <<EOF
{"name": "$n", "title": "$t", "arch": "mipsel-linux-musl", "cat": "$c", "files": ["usr/bin/$n", "start.sh", "stop.sh", "webui.conf"]}
EOF
	printf '%s|%s|%s\n' "$n" "$t" "$c" > "$P/.meta"
}

# 通用 start.sh：HOME 指到持久目录，日志进 /tmp，pid 落包目录
gen_start(){ # name cmd [need_config_file]
	local n="$1" cmd="$2" req="${3:-}"
	cat > "$ST/$n/start.sh" <<EOF
#!/bin/sh
# $n - started by the plugin center (pluginctl)
B=/etc/storage/plugins/$n
H=/etc/storage/plugins/.home/$n
mkdir -p "\$H"
P=\$B/run.pid
lstop(){ [ -f "\$P" ] && kill "\$(cat "\$P")" 2>/dev/null; sleep 1; rm -f "\$P"; }
lstop
EOF
	if [ -n "$req" ]; then
		cat >> "$ST/$n/start.sh" <<EOF
[ -f "\$H/$req" ] || { logger -t pluginctl "$n: 缺少 \$H/$req，请在插件设置页填好配置后再启动"; exit 0; }
EOF
	fi
	cat >> "$ST/$n/start.sh" <<EOF
cd "\$H" || exit 1
export HOME="\$H"
$cmd >>/tmp/$n.log 2>&1 &
echo \$! > "\$P"
EOF
	cat > "$ST/$n/stop.sh" <<EOF
#!/bin/sh
B=/etc/storage/plugins/$n
[ -f "\$B/run.pid" ] && kill "\$(cat "\$B/run.pid")" 2>/dev/null
killall $n 2>/dev/null
rm -f "\$B/run.pid"
EOF
	chmod +x "$ST/$n/start.sh" "$ST/$n/stop.sh"
}

pack(){ # name
	local n="$1"
	local f="$OUT/$n-v1.tar.gz"
	[ -f "$ST/$n/webui.conf" ] || : > "$ST/$n/webui.conf"
	tar czf "$f" -C "$ST" "$n"
	log "package $f $(stat -c%s "$f") bytes"
}

########################################################################
log "== ddns-go"
if V=$(fetch_src ddns-go jeessy2/ddns-go) && gobuild ddns-go . "$WORK/bin/ddns-go"; then
	stage ddns-go "DDNS-GO" dns
	cp "$WORK/bin/ddns-go" "$ST/ddns-go/usr/bin/ddns-go"
	gen_start ddns-go '$B/usr/bin/ddns-go -l :9876 -f 300'
	cat > "$ST/ddns-go/webui.conf" <<'EOF'
desc|说明|text|装好后浏览器打开 http://192.168.8.1:9876 配置域名与 DNS 服务商
EOF
	pack ddns-go; echo "ddns-go|DDNS-GO|ddns-go-v1.tar.gz|$(stat -c%s "$OUT/ddns-go-v1.tar.gz")|$(sha256sum "$OUT/ddns-go-v1.tar.gz"|cut -d' ' -f1)|dns" >> "$OUT/.index"
fi

log "== mosdns"
if V=$(fetch_src mosdns IrineSistiana/mosdns) && gobuild mosdns . "$WORK/bin/mosdns"; then
	stage mosdns "MosDNS" dns
	cp "$WORK/bin/mosdns" "$ST/mosdns/usr/bin/mosdns"
	cat > "$ST/mosdns/config.yaml" <<'EOF'
log:
  level: info
  file: ""
plugins:
  - tag: cache
    type: cache
    args:
      size: 2048
  - tag: forward_local
    type: forward
    args:
      concurrent: 2
      upstreams:
        - addr: udp://223.5.5.5
        - addr: udp://119.29.29.29
  - tag: main
    type: sequence
    args:
      - exec: $cache
      - exec: $forward_local
  - tag: local_udp
    type: udp_server
    args:
      entry: main
      listen: 0.0.0.0:5353
  - tag: local_tcp
    type: tcp_server
    args:
      entry: main
      listen: 0.0.0.0:5353
EOF
	gen_start mosdns '$B/usr/bin/mosdns start -c $B/config.yaml'
	cat > "$ST/mosdns/webui.conf" <<'EOF'
desc|说明|text|默认监听 0.0.0.0:5353，上游 223.5.5.5/119.29.29.29，改配置见插件目录 config.yaml
EOF
	# config.yaml 也要写进 manifest 的文件列表
	sed -i 's/"webui.conf"\]/"webui.conf", "config.yaml"]/' "$ST/mosdns/manifest.json"
	pack mosdns; echo "mosdns|MosDNS|mosdns-v1.tar.gz|$(stat -c%s "$OUT/mosdns-v1.tar.gz")|$(sha256sum "$OUT/mosdns-v1.tar.gz"|cut -d' ' -f1)|dns" >> "$OUT/.index"
fi

log "== nps"
if V=$(fetch_src nps ehang-io/nps) && gobuild nps ./cmd/nps "$WORK/bin/nps"; then
	stage nps "NPS内网穿透" tunnel
	cp "$WORK/bin/nps" "$ST/nps/usr/bin/nps"
	cat > "$ST/nps/nps.conf" <<'EOF'
[common]
bridge_port=8024
web_port=8080
web_username=admin
web_password=padavan
auth_key=padavan-nps-key
bridge_type=tcp
log_level=0
log_path=/tmp/nps.log
EOF
	gen_start nps '$B/usr/bin/nps -conf_path=$B/nps.conf'
	cat > "$ST/nps/webui.conf" <<'EOF'
desc|说明|text|管理面板 http://192.168.8.1:8080 账号 admin 密码 padavan，客户端连接端口 8024
EOF
	sed -i 's/"webui.conf"\]/"webui.conf", "nps.conf"]/' "$ST/nps/manifest.json"
	pack nps; echo "nps|NPS内网穿透|nps-v1.tar.gz|$(stat -c%s "$OUT/nps-v1.tar.gz")|$(sha256sum "$OUT/nps-v1.tar.gz"|cut -d' ' -f1)|tunnel" >> "$OUT/.index"
fi

log "== trojan-go"
if V=$(fetch_src trojan-go p4gefau1t/trojan-go) && gobuild trojan-go . "$WORK/bin/trojan-go"; then
	stage trojan-go "Trojan-Go" proxy
	cp "$WORK/bin/trojan-go" "$ST/trojan-go/usr/bin/trojan-go"
	cat > "$ST/trojan-go/config.example.json" <<'EOF'
{
  "run_type": "client",
  "local_addr": "127.0.0.1",
  "local_port": 1080,
  "remote_addr": "your.server.com",
  "remote_port": 443,
  "password": ["YOUR_PASSWORD"],
  "ssl": { "sni": "your.server.com" }
}
EOF
	gen_start trojan-go '$B/usr/bin/trojan-go -config $H/config.json' config.json
	cat > "$ST/trojan-go/webui.conf" <<'EOF'
desc|说明|text|把 config.example.json 复制成 /etc/storage/plugins/.home/trojan-go/config.json 填好服务器后启动
EOF
	sed -i 's/"webui.conf"\]/"webui.conf", "config.example.json"]/' "$ST/trojan-go/manifest.json"
	pack trojan-go; echo "trojan-go|Trojan-Go|trojan-go-v1.tar.gz|$(stat -c%s "$OUT/trojan-go-v1.tar.gz")|$(sha256sum "$OUT/trojan-go-v1.tar.gz"|cut -d' ' -f1)|proxy" >> "$OUT/.index"
fi

log "== gotty"
if V=$(fetch_src gotty sorenisanerd/gotty) && gobuild gotty . "$WORK/bin/gotty"; then
	stage gotty "GoTTY网页终端" term
	cp "$WORK/bin/gotty" "$ST/gotty/usr/bin/gotty"
	gen_start gotty '$B/usr/bin/gotty --port 7681 --permit-write --reconnect /bin/sh'
	cat > "$ST/gotty/webui.conf" <<'EOF'
desc|说明|text|浏览器打开 http://192.168.8.1:7681 直接操作路由器 shell
EOF
	pack gotty; echo "gotty|GoTTY网页终端|gotty-v1.tar.gz|$(stat -c%s "$OUT/gotty-v1.tar.gz")|$(sha256sum "$OUT/gotty-v1.tar.gz"|cut -d' ' -f1)|term" >> "$OUT/.index"
fi

log "== filebrowser"
if V=$(fetch_src filebrowser filebrowser/filebrowser) && gobuild filebrowser . "$WORK/bin/filebrowser"; then
	stage filebrowser "FileBrowser文件管理" nas
	cp "$WORK/bin/filebrowser" "$ST/filebrowser/usr/bin/filebrowser"
	gen_start filebrowser '$B/usr/bin/filebrowser -d $H/fb.db -p 8088 -a 0.0.0.0 -r /'
	cat > "$ST/filebrowser/webui.conf" <<'EOF'
desc|说明|text|浏览器打开 http://192.168.8.1:8088 首次进入创建管理员，可浏览全盘文件
EOF
	pack filebrowser; echo "filebrowser|FileBrowser文件管理|filebrowser-v1.tar.gz|$(stat -c%s "$OUT/filebrowser-v1.tar.gz")|$(sha256sum "$OUT/filebrowser-v1.tar.gz"|cut -d' ' -f1)|nas" >> "$OUT/.index"
fi

log "== speedtest-go"
if V=$(fetch_src speedtest-go showwin/speedtest-go) && gobuild speedtest-go . "$WORK/bin/speedtest-go"; then
	stage speedtest-go "网络测速(Speedtest)" net
	cp "$WORK/bin/speedtest-go" "$ST/speedtest-go/usr/bin/speedtest-go"
	cat > "$ST/speedtest-go/webui.conf" <<'EOF'
desc|说明|text|命令行工具：speedtest-go --server --all 选择节点，speedtest-go 直接测速
EOF
	pack speedtest-go; echo "speedtest-go|网络测速(Speedtest)|speedtest-go-v1.tar.gz|$(stat -c%s "$OUT/speedtest-go-v1.tar.gz")|$(sha256sum "$OUT/speedtest-go-v1.tar.gz"|cut -d' ' -f1)|net" >> "$OUT/.index"
fi

echo "================ SUMMARY"
cat "$OUT/.index" 2>/dev/null || true
ls -l "$OUT"

if [ "$PUB" = "1" ]; then
	command -v gh >/dev/null || { log "gh not found"; exit 1; }
	log "== publish to $REPO release $TAG"
	for f in "$OUT"/*.tar.gz; do gh release upload "$TAG" "$f" --repo "$REPO" --clobber; done
	log "== merge plugins.txt"
	mkdir -p "$WORK/idx"
	gh release download "$TAG" --repo "$REPO" --pattern plugins.txt --dir "$WORK/idx" >/dev/null 2>&1 || true
	[ -f "$WORK/idx/plugins.txt" ] || { log "FATAL: 拿不到线上 plugins.txt，拒绝覆盖"; exit 1; }
	# 安全阀：线上索引只剩个位数说明下载/解析出了问题，绝不能上传覆盖掉已有插件
	if [ "$(wc -l < "$WORK/idx/plugins.txt")" -lt 40 ]; then
		log "FATAL: 线上 plugins.txt 只有 $(wc -l < "$WORK/idx/plugins.txt") 行，拒绝覆盖"; exit 1
	fi
	cp "$WORK/idx/plugins.txt" "$WORK/plugins.txt"
	while IFS='|' read -r n t f s h c; do
		[ -n "$n" ] || continue
		grep -q "^$n|" "$WORK/plugins.txt" || printf '%s|%s|%s|%s|%s|%s\n' "$n" "$t" "$f" "$s" "$h" "$c" >> "$WORK/plugins.txt"
	done < "$OUT/.index"
	gh release upload "$TAG" "$WORK/plugins.txt" --repo "$REPO" --clobber
	log "plugins.txt now $(wc -l < "$WORK/plugins.txt") entries"
fi
