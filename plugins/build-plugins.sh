#!/bin/bash
# Build Padavan (mipsel-linux-musl) plugin packages from upstream GitHub sources,
# then publish them to the `plugin-repo` release and refresh plugins.txt.
#
# Runs on GitHub Actions (ubuntu) or any x86_64 Linux with go + upx + gh.
#   PUB=0  -> only build, keep artifacts in plugins/.out
#   PUB=1  -> also upload + update plugins.txt (needs GH_TOKEN)
#
# 重要：必须固定 Go 版本（GOTOOLCHAIN=local，配合 workflow 里的 setup-go 1.25）。
# 实测 go1.26 编出来的 mipsle 二进制在 K2P 4.4 内核上启动即崩
# （runtime.futexwakeup 返回 -89 + SIGSEGV）；go1.23/1.25 编的同源二进制正常。
set -u

REPO="${REPO:-jonepjxh/padavan-TurBoTse}"
TAG="${TAG:-plugin-repo}"
PUB="${PUB:-0}"
PKGVER="${PKGVER:-v3}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/plugins/.work"
OUT="$ROOT/plugins/.out"
ST="$WORK/stage"
rm -rf "$WORK"; mkdir -p "$WORK" "$OUT" "$ST"

export CGO_ENABLED=0 GOOS=linux GOARCH=mipsle GOMIPS=softfloat GOFLAGS=-mod=mod
export GOTOOLCHAIN=local

log(){ echo "[build] $*"; }
: > "$OUT/.index"

# 上游源码（浅克隆）到 $WORK/src/<name>，$3 可选 tag/branch
fetch_src(){ # name repo [ref]
	local n="$1" r="$2" ref="${3:-}"
	if [ ! -d "$WORK/src/$n" ]; then
		if [ -n "$ref" ]; then
			git clone --depth 1 --branch "$ref" "https://github.com/$r.git" "$WORK/src/$n" >/dev/null 2>&1 || return 1
		else
			git clone --depth 1 "https://github.com/$r.git" "$WORK/src/$n" >/dev/null 2>&1 || return 1
		fi
	fi
	( cd "$WORK/src/$n" && git describe --tags --always 2>/dev/null | head -1 )
}

# 交叉编译：name pkgpath
gobuild(){ # name pkgpath
	local n="$1" p="$2"
	( cd "$WORK/src/$n" && go build -buildvcs=false -trimpath -ldflags "-s -w" -o "$WORK/bin/$n" "$p" ) >"$WORK/$n.build.log" 2>&1 || {
		log "BUILD FAIL $n"; tail -8 "$WORK/$n.build.log"; return 1; }
	[ -f "$WORK/bin/$n" ] || return 1
	if command -v upx >/dev/null 2>&1; then
		upx -9 -q "$WORK/bin/$n" >/dev/null 2>&1 || true
	fi
	log "built $n $(stat -c%s "$WORK/bin/$n") bytes"
}

# 前端资源：filebrowser（pnpm）、message-pusher（npm）这类仓库把 dist 放在 .gitignore，
# 不跑一遍前端构建，Go 的 //go:embed 就抓不到文件，装到路由上只会 404 白屏。
build_frontend(){ # dir
	local d="$1"
	[ -f "$d/package.json" ] || return 0
	log "frontend build: $d"
	if [ -f "$d/pnpm-lock.yaml" ]; then
		( cd "$d" && pnpm install --frozen-lockfile && pnpm run build ) >"$WORK/frontend.log" 2>&1 || {
			log "FRONTEND FAIL $d"; log "$(tail -20 "$WORK/frontend.log" 2>&1 | tr '\n' ' ')"; return 1; }
	else
		( cd "$d" && npm install --no-audit --no-fund --legacy-peer-deps && npm run build ) >"$WORK/frontend.log" 2>&1 || {
			log "FRONTEND FAIL $d"; tail -15 "$WORK/frontend.log"; return 1; }
	fi
	log "frontend ok: $d"
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
[ -f "\$H/$req" ] || { logger -t pluginctl "$n: 缺少 \$H/$req，请在插件配置里填好后再启动"; exit 0; }
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

# 打包并写索引（size/sha256 取自真实文件；早期版本索引里错指 -v1.tar.gz，两列是空的）
pack(){ # name title cat
	local n="$1" t="$2" c="$3"
	local f="$OUT/$n-$PKGVER.tar.gz" sz hx
	[ -f "$ST/$n/webui.conf" ] || : > "$ST/$n/webui.conf"
	# 归档根目录必须是包内容本身（pluginctl 解包到 /tmp/plugins/<name>，
	# 多套一层 <name>/ 会导致 usr/bin/<bin> 找不到，实测会 “not found”）
	tar czf "$f" --owner=0 --group=0 --numeric-owner -C "$ST/$n" .
	sz=$(stat -c%s "$f"); hx=$(sha256sum "$f" | cut -d' ' -f1)
	printf '%s|%s|%s|%s|%s|%s\n' "$n" "$t" "$(basename "$f")" "$sz" "$hx" "$c" >> "$OUT/.index"
	log "package $(basename "$f") $sz bytes"
}

########################################################################
# ddns-go：固定 v6.10.0（go1.23.6）。v6.18.0 的 go.mod 要求 go1.26，
# 会逼 CI 拉 1.26 工具链，编出来的二进制在 4.4 内核上启动即崩。
log "== ddns-go"
if V=$(fetch_src ddns-go jeessy2/ddns-go v6.10.0) && gobuild ddns-go .; then
	stage ddns-go "DDNS-GO" dns
	cp "$WORK/bin/ddns-go" "$ST/ddns-go/usr/bin/ddns-go"
	gen_start ddns-go '$B/usr/bin/ddns-go -l :9876 -f 300'
	cat > "$ST/ddns-go/webui.conf" <<'EOF'
desc|说明|text|装好后浏览器打开 http://192.168.8.1:9876 配置域名与 DNS 服务商
EOF
	pack ddns-go "DDNS-GO" dns
fi

log "== mosdns"
if V=$(fetch_src mosdns IrineSistiana/mosdns) && gobuild mosdns .; then
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
	sed -i 's/"webui.conf"\]/"webui.conf", "config.yaml"]/' "$ST/mosdns/manifest.json"
	pack mosdns "MosDNS" dns
fi

log "== trojan-go"
if V=$(fetch_src trojan-go p4gefau1t/trojan-go) && gobuild trojan-go .; then
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
	pack trojan-go "Trojan-Go" proxy
fi

log "== speedtest-go"
if V=$(fetch_src speedtest-go showwin/speedtest-go v1.8.0) && gobuild speedtest-go .; then
	stage speedtest-go "网络测速(Speedtest)" net
	cp "$WORK/bin/speedtest-go" "$ST/speedtest-go/usr/bin/speedtest-go"
	cat > "$ST/speedtest-go/webui.conf" <<'EOF'
desc|说明|text|命令行工具：speedtest-go --server --all 选择节点，speedtest-go 直接测速
EOF
	pack speedtest-go "网络测速(Speedtest)" net
fi

log "== acme.sh"
# 纯 shell 插件（不用编译）：证书申请与自动续期，配合 cron 用
if V=$(fetch_src acme.sh acmesh-official/acme.sh) && [ -f "$WORK/src/acme.sh/acme.sh" ]; then
	P="$ST/acme.sh"
	mkdir -p "$P/usr/bin" "$P/dnsapi" "$P/deploy"
	cp "$WORK/src/acme.sh/acme.sh" "$P/usr/bin/acme.sh"
	cp -f "$WORK/src/acme.sh/dnsapi"/*.sh "$P/dnsapi" 2>/dev/null
	cp -f "$WORK/src/acme.sh/deploy"/*.sh "$P/deploy" 2>/dev/null
	chmod +x "$P/usr/bin/acme.sh"
	cat > "$P/manifest.json" <<'EOF'
{"name": "acme.sh", "title": "ACME证书", "arch": "noarch", "cat": "cert", "files": ["usr/bin/acme.sh", "install.sh", "webui.conf"]}
EOF
	cat > "$P/install.sh" <<'EOF'
#!/bin/sh
# 建立持久工作目录，acme.sh 的证书与账号都放这里
H=/etc/storage/plugins/.home/acme.sh
mkdir -p "$H"
echo "acme.sh ready: export LE_WORKING_DIR=$H"
EOF
	chmod +x "$P/install.sh"
	cat > "$P/webui.conf" <<'WEOF'
desc|说明|text|命令行工具：acme.sh --issue -d 域名 --standalone 或 --dns dns_ali（需先 export 阿里云 Key）；证书默认存 /etc/storage/plugins/.home/acme.sh
WEOF
	pack acme.sh "ACME证书" misc
fi

log "== lucky"
if V=$(fetch_src lucky gdy666/lucky v3.5.0) && gobuild lucky .; then
	stage lucky "Lucky" tunnel
	cp "$WORK/bin/lucky" "$ST/lucky/usr/bin/lucky"
	gen_start lucky '$B/usr/bin/lucky -p 16601'
	cat > "$ST/lucky/webui.conf" <<'EOF'
desc|说明|text|管理面板 http://192.168.8.1:16601 默认账号 666 密码 666；端口转发/反代/DDNS/STUN 穿透/证书都在这里配
EOF
	pack lucky "Lucky" tunnel
fi

log "== filebrowser"
if V=$(fetch_src filebrowser filebrowser/filebrowser v2.63.23) && build_frontend "$WORK/src/filebrowser/frontend" && gobuild filebrowser .; then
	stage filebrowser "FileBrowser文件管理" nas
	cp "$WORK/bin/filebrowser" "$ST/filebrowser/usr/bin/filebrowser"
	gen_start filebrowser '$B/usr/bin/filebrowser -d $H/fb.db -p 8088 -a 0.0.0.0 -r /'
	cat > "$ST/filebrowser/webui.conf" <<'EOF'
desc|说明|text|浏览器打开 http://192.168.8.1:8088 首次进入用 admin/admin 登录后改密码，可浏览全盘文件
EOF
	pack filebrowser "FileBrowser文件管理" nas
fi

log "== cfddns"
if V=$(fetch_src cfddns favonia/cloudflare-ddns v1.14.0) && gobuild cfddns ./cmd/ddns; then
	stage cfddns "CF域名解析" dns
	cp "$WORK/bin/cfddns" "$ST/cfddns/usr/bin/cloudflare-ddns"
	cat > "$ST/cfddns/env.example" <<'EOF'
CLOUDFLARE_API_TOKEN=在这里填你的 Cloudflare API Token
DOMAINS=home.example.com
PROXIED=true
EOF
	cat > "$ST/cfddns/start.sh" <<'EOF'
#!/bin/sh
B=/etc/storage/plugins/cfddns
H=/etc/storage/plugins/.home/cfddns
mkdir -p "$H"
[ -f "$H/run.pid" ] && kill "$(cat "$H/run.pid")" 2>/dev/null
[ -f "$H/env" ] || cp "$B/env.example" "$H/env"
. "$H/env"
case "$CLOUDFLARE_API_TOKEN" in ""|*"在这里填"*) logger -t pluginctl "cfddns: 先在 /etc/storage/plugins/.home/cfddns/env 里填 API Token"; exit 0;; esac
cd "$H" || exit 1
export HOME="$H"
"$B/usr/bin/cloudflare-ddns" >>/tmp/cfddns.log 2>&1 &
echo $! > "$H/run.pid"
EOF
	cat > "$ST/cfddns/stop.sh" <<'EOF'
#!/bin/sh
H=/etc/storage/plugins/.home/cfddns
[ -f "$H/run.pid" ] && kill "$(cat "$H/run.pid")" 2>/dev/null
killall cloudflare-ddns 2>/dev/null
rm -f "$H/run.pid"
EOF
	chmod +x "$ST/cfddns/start.sh" "$ST/cfddns/stop.sh"
	cat > "$ST/cfddns/webui.conf" <<'EOF'
desc|说明|text|编辑 /etc/storage/plugins/.home/cfddns/env 填 CLOUDFLARE_API_TOKEN 和 DOMAINS，然后在插件中心启用
EOF
	pack cfddns "CF域名解析" dns
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
		if grep -q "^$n|" "$WORK/plugins.txt"; then
			awk -F'|' -v n="$n" -v t="$t" -v f="$f" -v s="$s" -v h="$h" -v c="$c" \
				'BEGIN{OFS="|"} $1==n{$1=n;$2=t;$3=f;$4=s;$5=h;$6=c} {print}' \
				"$WORK/plugins.txt" > "$WORK/p.txt" && mv "$WORK/p.txt" "$WORK/plugins.txt"
		else
			printf '%s|%s|%s|%s|%s|%s\n' "$n" "$t" "$f" "$s" "$h" "$c" >> "$WORK/plugins.txt"
		fi
	done < "$OUT/.index"
	gh release upload "$TAG" "$WORK/plugins.txt" --repo "$REPO" --clobber
	log "plugins.txt now $(wc -l < "$WORK/plugins.txt") entries"
fi
