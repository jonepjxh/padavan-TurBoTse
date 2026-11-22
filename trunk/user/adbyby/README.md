# NanoAd for K2P / Padavan 4.4

NanoAd 是面向 Phicomm K2P（MT7621，128 MB RAM）和 Padavan 4.4 的轻量网络广告过滤器。它用一份约 123 KiB 的静态 MIPS 程序同时提供 DNS 过滤、HTTPS SNI 过滤和 WebUI，不依赖 Go、Python、Node.js、OpenSSL 或证书中间人。

## 主要特点

- **DNS 过滤**：dnsmasq 继续负责 DHCP、缓存和现有域名规则；NanoAd 只监听本机 `127.0.0.1:5353` 做一次内存哈希判断。
- **HTTPS 过滤**：可选透明 SNI 代理读取 TLS ClientHello 中的域名，命中规则后断开；不解密内容，不需要在手机/电视安装 CA 证书。
- **规则兼容**：导入 hosts、dnsmasq `address=/domain/`、纯域名、AdGuard/ABP 的 `||domain^` 和 `@@||domain^`。
- **节省内存**：规则离线编译成二进制，三个进程用只读 `mmap` 共享；100,000 条规则文件约 5 MiB，实际常驻按访问页增长。
- **原子更新**：先下载到 `/tmp`，校验、编译成功后才 `rename` 并通知进程重载；失败保留旧规则。
- **WebUI**：状态统计、DNS/SNI 开关、订阅管理、黑白名单粘贴/文件导入、域名命中测试、更新日志。
- **安全默认值**：DNS 仅监听本机；管理页只允许 `br0`；独立随机密码 + Basic Auth + POST CSRF token；规则下载只当文本解析，不执行脚本。

## 先说明 HTTPS 的边界

DNS 过滤能覆盖绝大多数“独立广告域名”，并且对 HTTP/HTTPS 都有效。SNI 过滤进一步处理 DNS 未命中、但仍使用独立 TLS 域名的 HTTPS 连接。两种方式都**不能按 URL 路径删除与正文同域的广告**，例如 YouTube 贴片、部分视频 App 的前贴片、网页内嵌赞助内容。

真正的内容级 HTTPS 过滤必须在路由器解密 TLS：需要 CA 证书、OpenSSL 级 CPU 开销和大量 RAM，还会让所有客户端信任一张路由器的根证书。K2P 的 MT7621 不适合，也存在明显安全风险，因此本插件不包含该模式。需要网页内 URL 级过滤时，请配合浏览器端 uBlock Origin 等扩展。

SNI 模式的实际限制：

- 仅 TCP/443；QUIC（UDP/443）默认直连。可在 WebUI 打开“阻断 QUIC”，强制浏览器回退 TCP。
- 无 SNI、ECH 加密 ClientHello、或 ClientHello 超过 8 KiB 时默认放行（fail-open）。
- 不处理证书校验、内容改写，也不影响正常 TLS 端到端加密。

## 性能设计

| 模块 | 设计 | 典型内存 |
|---|---|---|
| 规则引擎 | FNV-1a + 开放寻址哈希表；按域名后缀最多查 3–5 次，不做逐条线性匹配；父域规则会剪掉重复子域 | 100k 条约 5 MiB 文件，mmap 共享 |
| DNS | 单线程 poll；128 个并发上游请求；UDP 客户端/上游 + TCP 客户端/截断重试；超时切换上游 | 约 1–4 MiB（含工作缓冲） |
| HTTPS SNI | 单线程 poll、非阻塞双向转发、无 TLS 解密；96 个连接上限，每连接 2×8 KiB 缓冲 | 最高约 1.6 MiB 缓冲 |
| WebUI | 无框架、无 CGI；一次处理一个本机管理请求 | 请求时约 1 MiB 内 |

本机 x86 参考测试（仅用于对比算法开销）：100,000 条规则编译 0.046 秒，规则命中约 660 万次/秒；DNS 进程加载 100k 条规则时 RSS 约 3.4 MiB，命中响应约 0.1 ms。MT7621 会慢很多，但家庭 DNS 峰值通常远低于它的处理上限。

规则规模建议：128 MB 内存设备的最终域名数建议 **不超过约 120,000 条**；默认 anti-AD 官方 CDN 订阅编译后约 10 万条，在本引擎下只占约 5 MiB 规则文件。不要同时叠加多个大型列表，否则误杀概率和编译时间都会上升。默认地址 `https://anti-ad.net/anti-ad-for-dnsmasq.conf` 在国内网络实测可下载；GitHub 源只作为可选备用，下载失败会保留旧规则。

## 安装

要求：K2P 已刷 Padavan 4.4，能 SSH 登录，路由器可访问互联网。

1. 把 `nanoad-k2p-1.0.0.tar.gz` 上传到 `/tmp`。
2. SSH 执行：

```sh
cd /tmp
tar -xzf nanoad-k2p-1.0.0.tar.gz
cd nanoad-k2p-1.0.0
sh install.sh
```

安装脚本会：

- 把程序安装到 `/etc/storage/nanoad`；
- 生成 32 位随机管理密码；
- 首次下载默认订阅并编译规则；
- 在 `/etc/storage/post_wan_script.sh` 的 WAN up 分支增加开机启动（`nanoad-ctl boot`，不重复写 flash）；没有该文件的固件回退到 `post_start.sh`；
- 在 `/etc/storage/post_iptables_script.sh` 增加防火墙恢复；
- 把 dnsmasq 的上游指向 `127.0.0.1:5353`，并备份原 `dnsmasq.servers` 为 `dnsmasq.servers.pre-nanoad`；
- 保存 `/etc/storage`。

完成后终端会打印：

```text
WebUI:    http://192.168.2.1:18080
Username: admin
Password: <32位随机密码>
```

DNS 过滤默认开启，HTTPS SNI 默认关闭。登录 WebUI 打开“HTTPS SNI 过滤”并保存即可启用透明代理；需要处理 QUIC 视频流量时再打开“阻断 QUIC”。

如果首次订阅下载失败，安装会继续启动 WebUI，但使用空规则**放行全部 DNS**。进入页面粘贴规则或点击“立即更新”，成功后再重启服务，即恢复正常拦截；这比安装失败后直接断网更安全。

## WebUI 使用

- **过滤开关**：DNS、SNI、QUIC；保存后服务约 1 秒后重启。
- **DNS 上游**：最多 4 个 `IP:端口`，逗号分隔。建议保留两个不同运营商/公共 DNS。
- **拦截响应**：`NXDOMAIN` 兼容性最好；某些电视 App 对 NXDOMAIN 会重试，可改 `0.0.0.0 / ::`。
- **第三方订阅**：每行一个 URL。支持 `http`/`https`；不限制文件后缀。
- **本地导入**：选择 `.txt`/`.list` 后追加，或直接粘贴覆盖。黑名单按后缀拦截，白名单优先。
- **域名测试**：直接查询编译后的规则，不发起真实 DNS 请求。
- **立即更新**：后台运行 `nanoad-update`；页面会轮询状态和日志。

首次打开页面浏览器会弹 Basic Auth，默认用户名和密码均为 `admin`。登录后请立即在 WebUI 的“管理账号”里修改，也可以在 `/etc/storage/nanoad/nanoad.conf` 修改 `web_user=` 与 `web_token=`。

## 规则语法

支持：

```text
# hosts（精确域名）
0.0.0.0 ads.example.com
127.0.0.1 tracker.example.net

# dnsmasq（后缀）
address=/ads.example.com/0.0.0.0
address=/one.example/two.example/0.0.0.0

# 纯域名（后缀）
ads.example.com

# AdGuard / ABP 域名规则（后缀）
||ads.example.com^
||ads.example.com^$important
@@||login.example.com^
```

跳过：URL 路径规则（含 `://`）、正则 `/.../`、元素隐藏 `##`/`#@#`、带 `$third-party`/`$client` 等无法在纯 DNS 层准确表达的修饰符。这样宁可少拦，也避免把整个站点误杀。

白名单优先于任何父域黑名单。例如：

```text
blockparent.example
child.blockparent.example       # 编译时被父域剪掉
@@allowed.child.blockparent.example
```

## 更新与手动控制

```sh
/etc/storage/nanoad/nanoad-ctl status
/etc/storage/nanoad/nanoad-ctl restart
/etc/storage/nanoad/nanoad-ctl stop
/etc/storage/nanoad/nanoad-ctl start
/etc/storage/nanoad/nanoad-update
```

手动 `start`、`stop`、`restart` 会调用 `mtd_storage.sh save` 保存配置和 dnsmasq 状态；开机钩子使用 `boot`，避免每次开机重复写 flash。

更新日志：`/tmp/nanoad/update.log`。配置文件：`/etc/storage/nanoad/nanoad.conf`。编译后的规则：`/tmp/nanoad/rules.bin`。

K2P 的 `Storage` 分区只有 512 KB（`mtdblock6`），放不下 10 万条规则的完整副本。因此持久化的是 `rules.compact.txt.gz`：约 4.4 万条，优先保留常见广告/追踪域，再从全量规则中均匀抽样补齐。开机时解压并编译到 `/tmp`，不占常驻内存。联网更新成功后会先得到完整规则（约 10 万条）在内存中生效，同时重建这份精简副本供下次开机使用。

自动更新默认关闭。可在 `nanoad.conf` 设 `auto_update=1` 并重启服务，`nanoad-ctl` 会注册每天 04:23 的 `cru` 任务。

## 兼容性与冲突

- 默认接管 dnsmasq 的普通上游。若同时使用 SmartDNS、AdGuard Home、mosdns、Clash 的 DNS 功能，只保留一个入口，否则会互相覆盖。
- 自定义 dnsmasq 中按域名指定服务器的规则会在启用期间移出 `dnsmasq.servers`；停止 NanoAd 时自动恢复备份。
- SNI 重定向使用 `iptables -t nat` 的 `REDIRECT` 和 `SO_ORIGINAL_DST`。若固件精简掉了该扩展，DNS 过滤仍可使用，SNI 开关会无效。
- HTTPS SNI 过滤默认关闭。实机测试能拦截 `doubleclick.net` 等目标，但部分站点握手失败率偏高；建议只在需要时开启，出现访问异常先关闭。
- 透明代理出口是原始目标地址，不改变 DNS、不改写证书，也不支持 IPv6 的 NAT 重定向；K2P 家庭网络一般使用 IPv4。
- 管理页是 HTTP。只在 LAN 使用，不建议在不可信 Wi-Fi 上登录。

## 卸载

```sh
sh /etc/storage/nanoad/uninstall.sh
```

会停止进程、移除 NAT/INPUT 规则、恢复 dnsmasq 配置、删除开机钩子和安装目录。

## 从源码编译

本机测试：

```sh
make
make test
```

交叉编译 K2P（Padavan 4.4 工具链）：

```sh
make clean
make CROSS_COMPILE=/path/to/toolchain/bin/mipsel-linux-uclibc- \
     CFLAGS='-Os -g0 -std=c99 -Wall -Wextra -fno-strict-aliasing -ffunction-sections -fdata-sections -D_FILE_OFFSET_BITS=64' \
     LDFLAGS='-Wl,--gc-sections -static'
$CROSS_COMPILE-strip --strip-all nanoad
```

动态链接版约 50 KiB，但依赖固件 uClibc 版本；发布包默认使用 123 KiB 静态链接版，兼容性更好。使用的工具链是 Padavan 4.4 的 `mipsel-linux-uclibc`，目标为 MIPS32r2 little-endian。

测试覆盖：规则编译/剪枝/白名单、真实 OpenSSL ClientHello 的 SNI 解析、DNS UDP/TCP 转发与拦截、Web 鉴权、CSRF、规则追加导入。

## 目录

```text
src/           C 源码（规则、DNS、SNI、WebUI 服务）
config/        默认配置和规则模板
scripts/       nanoad-ctl、nanoad-update
web/           单页 WebUI
tests/         规则与 DNS/Web 集成测试
install.sh     路由器安装
uninstall.sh   卸载
```

## 参考与借鉴

- AdGuard Home 的规则语法与“DNS 层无法处理同域广告”的边界说明：<https://adguard-dns.io/kb/adguard-home/faq/>
- AdGuard DNS 过滤语法：<https://adguard-dns.io/kb/general/dns-filtering-syntax/>
- OpenWrt adblock 的列表规模建议和 TLD 剪枝思路：<https://github.com/openwrt/packages/blob/master/net/adblock/files/README.md>
- Padavan 4.4 与 K2P 工具链：<https://github.com/hanwckf/padavan-4.4>

本项目为独立实现，未复制上述项目代码。许可证见 `LICENSE`。
