# padavan #

This project is based on original rt-n56u with latest mtk 4.4.198 kernel, which is fetch from D-LINK GPL code.

#### Extra functions / changes
- Adding user/chinadns-ng , and fix shadowsocks + chinadns-ng using local domain whitellist.
- AP Relay auto-daemon


#### SS/SSR
- Transparent proxy (iptables) wasn't cleaned completely, this issue is fixed.
- Adding DNSProxy , Local DNS integrated with SS/SSR 
- Dnsmasq optimization specially for SS/SSR 
- Resolve DNS pollution - Adding DNS i/p in china-route mode
- Fast-open option is enabled according to linux version
##### Enhancements in this repo

- commits has beed rewritten on top of [hanwckf/rt-n56u](https://github.com/hanwckf/rt-n56u) repo for better history tracking
- Optimized Makefiles and build scripts, added a toplevel Makefile
- Added ccache support, may save up to 50%+ build time
- Upgraded the toolchain and libc:
  - gcc 13.3.0
  - musl 1.2.5 / uClibc-ng 1.0.52
 - OpenWrt style package Makefile
 - Enabled kernel cgroups support
 - Fixed K2P led label names
 - Replaced udpxy with msd_lite
 - Replaced Web Console with ttyd
 - Upgraded libs and user packages
 - And a lot of package related fixes
 - ...

# Features

- Based on 4.4.198 Linux kernel
- Support MT7621 based devices
- Support MT7615D/MT7615N/MT7915D wireless chips
- Support raeth and mt7621 hwnat with legency driver
- Support qca shortcut-fe
- Support IPv6 NAT based on netfilter
- Support WireGuard integrated in kernel
- Support fullcone NAT (by Chion82)
- Support LED&GPIO control via sysfs

# Supported devices
- 360-T6M (from https://github.com/BINGHUAice/padavan-4.4, 没有机器测试，自行判断)
- B70
- BELL-A040WQ
- C-Life-XG1 (from https://github.com/vb1980/padavan-4.4, 没有机器测试，自行判断)
- CR660x
- DIR-878
- DIR-882
- EA7500 (from https://github.com/MNM28/padavan-4.4, 没有机器测试，自行判断)
- G-AX1800 (from https://github.com/ddyjyj/padavan-4.4, 富春江G-AX1800, 自测可用)
- G-AX1800-B (from https://github.com/vb1980/padavan-4.4, 富春江G-AX1800黑色版本, 自测可用, 网口配置和白色版不一样, 4根天线都是ipex可拆, ttl排针焊接好了, 做工比白色版好些)
- GHL (from https://github.com/fangenhui520/padavan-4.4, 没有机器测试，自行判断)
- HAR-20S2U1 (from https://github.com/vb1980/padavan-4.4, 没有机器测试，自行判断)
- JDCloud RE-CP-02 (无线宝鲁班, from https://github.com/240038901/padavan-4.4, 没有机器测试，自行判断)
- JDCloud RE-SP-01B (from https://github.com/MeIsReallyBa/padavan-4.4, 没有机器测试，自行判断)
- JCG-836PRO
- JCG-AC860M
- JCQ-Q10Pro (from https://github.com/vb1980/padavan-4.4, 没有机器测试，自行判断)
- JCQ-Q11Pro (from https://github.com/qewwqewq22/padavan11, 没有机器测试，自行判断)
- JCG-Q20
- JCG-Y2
- K2P
- K2P-USB
- KOMI-A8 (from https://github.com/vb1980/padavan-4.4, 没有机器测试，自行判断)
- MI-4
- MI-R3G
- MI-R3P
- MI-R4A (from https://github.com/vipshmily/padavan-4.4, 自测可用)
- MR2600
- MSG1500
- MSG1500-Z (from https://github.com/TurBoTse/padavan, 自测可用)
- NETGEAR-BZV
- NETGEAR-R6800 (from https://github.com/MNM28/padavan-4.4, 没有机器测试，自行判断)
- NETGEAR-R7450 (from https://github.com/vipshmily/padavan-4.4, 自测可用)
- NEWIFI
- NEWIFI3 (partially from https://github.com/GH-X/padavan-4.4, 无需拆除C48电容, 没有机器测试，自行判断)
- QM-B1 (from https://github.com/monw/padavan, 没有机器测试，自行判断)
- R2100
- RM2100
- RT-AC85P
- SIM-AX1800T (from https://github.com/vb1980/padavan-4.4, 没有机器测试，自行判断)
- TX1801 Plus (from https://github.com/MNM28/padavan-4.4, 没有机器测试，自行判断)
- WDR8620 (from https://github.com/fzn0268/padavan-4.4, 没有机器测试，自行判断)
- WRL18M4GD (from https://github.com/vb1980/padavan-4.4, 没有机器测试，自行判断)
- WE410443-TC (from https://github.com/akw28888/padavan-4.4, 没有机器测试，自行判断)
- WIA3300-10 (from https://github.com/vb1980/padavan-4.4, 西加云杉WIA3300-10, 自测可用)
- WR1200JS
- XY-C1
- ZTE-E8820S
- ZTE-E8820V2
- ZTT-RX6000(from https://github.com/vb1980/padavan-4.4, 中天科技RX6000的Wifi6运营商机器, ttl刷机简单, 使用R3G分区, r3g breed刷入, 自测可用)
# Compilation steps

- Install dependencies
  ```sh
  # Debian/Ubuntu
  sudo apt install unzip libtool-bin ccache curl cmake gperf gawk flex bison nano xxd \
      fakeroot kmod cpio bc zip git python3-docutils gettext automake autopoint \
      texinfo build-essential help2man pkg-config zlib1g-dev libgmp3-dev \
      libmpc-dev libmpfr-dev libncurses5-dev libltdl-dev wget libc-dev-bin liblzma-dev
  ```
  **Optional:**
  - install [golang](https://go.dev/doc/install) for building go programs
    ```sh
    sudo rm -rf /usr/local/go
    curl -fsSL https://go.dev/dl/go1.20.10.linux-amd64.tar.gz | sudo tar -C /usr/local -xz
    echo "export PATH=\$PATH:/usr/local/go/bin" | sudo tee /etc/profile.d/go.sh
    source /etc/profile.d/go.sh
    go version
    ```
  - install [nodejs](https://nodejs.org/en/download) for building [AdGuardHome](trunk/user/adguardhome)
    ```sh
    sudo apt update
    sudo apt install -y ca-certificates curl gnupg
    sudo mkdir -p /etc/apt/keyrings
    curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key | sudo gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg
    echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_18.x nodistro main" | sudo tee /etc/apt/sources.list.d/nodesource.list
    sudo apt update
    sudo apt install -y nodejs
    node -v
    ```
- Clone source code
  ```sh
  git clone https://github.com/TurBoTse/padavan.git
  ```
- Modify template file and start compiling
  ```sh
  # (Optional) Modify template file
  # vi trunk/configs/templates/K2P.config

  # Start compiling with: make PRODUCT_NAME
  make K2P

  # To build firmware for other devices, clean the tree after previous build
  make clean
  ```

# Package Development

- Makefile examples
  - [Makefile project](trunk/libs/libpcre/Makefile) 
  - [CMake project](trunk/user/ttyd/Makefile)
- Compiling a single package (cd to `trunk` first)
  - build: `make libs/libpcre_only`
  - clean: `make libs/libpcre_clean`
  - romfs: `make libs/libpcre_romfs`

# Manuals

- Controlling GPIO and LEDs via sysfs
- How to use NAND RWFS partition
- How to use IPv6 NAT and fullcone NAT
- How to add new device support with device tree

# 插件中心（Plugin Center）

Padavan 4.4 本身是「插件全部编进固件」的模式：想要的功能必须在编译前写进
`config` 模板里，刷好机之后没法再装。这里把固件改成 OpenWrt 那种「插件仓库 +
在线安装」的用法：固件只保留一套插件框架，插件包放到 GitHub Release，用户在 Web
后台点「安装」即可，装完不用重启、不用重刷固件，左侧菜单会自动出现入口。

## 菜单长什么样

左侧「高级设置」下面，**插件中心固定排在「系统日志」之后**：

```
系统日志
插件中心          <- 本功能，始终存在
<分类目录 1..N>   <- 只在装了对应分类的插件后才出现
```

插件中心页里是插件仓库的完整清单（名字、分类、大小、状态），每个插件可以
「安装 / 启用 / 停用 / 卸载」，也可以一键「同步仓库」拉取最新清单。

装好的插件**自动分类**到对应目录，装完刷新页面就能看到，卸载后入口自动消失：

| 分类 | 目录 | 命中示例 |
| --- | --- | --- |
| ad | 广告管理 | adbyby、adguardhome、adblock、nanoad |
| dns | DNS服务 | smartdns、dnsmasq、chinadns、dnsproxy、aliddns |
| proxy | 科学上网 | xray、v2ray、clash、shadowsocks、trojan、singbox |
| tunnel | 穿透服务 | frp、zerotier、ddnsto、tailscale、wireguard |
| nas | 存储下载 | alidriver、aria2、transmission、xupnpd、vsftpd |
| net | 网络工具 | iperf3、socat、mtr、tcpdump、lrzsz、msd_lite |
| term | 终端运维 | ttyd、htop、nano |
| campus | 校园网认证 | mentohust、u2ec、scutclient、dogcom、minieap |
| qos | QOS流控 | sqm、cake |
| misc | 其他插件 | 未命中的其它包 |

分类判定顺序：插件包 `web/menu.conf` 第三列 > 插件仓库索引里的 `cat` 字段 >
按包名关键字匹配 > 兜底「其他插件」。

**同名目录复用**：固件里本来就有「广告管理 / DNS服务 / 科学上网 / 穿透服务」
这些目录时，插件直接挂进原有目录（成为该目录下的一个新标签页），不会出现两个
同名目录；只有固件没有的目录（例如「终端运维」）才在「插件中心」后面新建。

## 组成

| 部件 | 文件 | 作用 |
| --- | --- | --- |
| 菜单注入 | `trunk/user/httpd/src/web_ex.c` | `state.js` 由 `do_js()` 输出，输出末尾动态追加 `menuL2_title/tabtitle/tablink` 的 splice/push，内容来自 `/etc/storage/plugins/*` 的实时扫描 |
| 页面路由 | `trunk/user/httpd/src/httpd.c` | `/plugin/<pkg>/<file>` 映射到 `/etc/storage/plugins/<pkg>/web/<file>`，包名校验、禁止 `..` 与反斜杠 |
| 插件中心页 | `trunk/user/www/n56u_ribbon_fixed/Advanced_plugincenter.asp` | 清单、分类筛选、安装/启用/停用/卸载、同步仓库 |
| 分类页 | `Advanced_plugincat.asp` | 新建分类目录的落地页，列出该分类下已装插件 |
| 通用管理页 | `Advanced_plugincfg.asp` | 纯二进制插件的统一管理页：状态、启停、自启、`webui.conf` 配置表单 |
| 安装器 | `trunk/user/scripts/files/usr/bin/pluginctl` | 纯 busybox sh，无 python/jq：sync / list / install / remove / start / stop / enable / disable / menu / setcfg |
| 字典 | `trunk/user/www/dict/CN.dict` | `menu5_40=插件中心`、`menu5_41=插件设置` |

这些文件都已经直接提交在源码树里，`git clone` 后 `make K2P` 出来的固件自带插件
中心；仓库根目录的 `plugincenter-overlay.tar.gz` 只是给 CI 用的同一份文件的打包
（工作流从上游 tarball 构建时覆盖进去），两边内容保持一致。

## 插件包结构

```
<plugin>.tar.gz
|-- manifest.json            # {"name":"smartdns","title":"SmartDNS"}，有它就一定有菜单入口
|-- install.sh               # 可选，解包后执行一次
|-- start.sh / stop.sh       # 可选，安装后「启用」会调用
|-- uninstall.sh             # 可选，卸载前执行
|-- web/                     # 可选，插件自带管理页面
|   |-- menu.conf            # 菜单名|页面文件|分类
|   |-- index.asp            # 没写 menu.conf 时的默认页
|   `-- Advanced_xxx.asp
`-- usr/bin/xxx             # 可执行文件，安装时自动软链到 /opt/bin
```

安装目录是 `/etc/storage/plugins/<pkg>/`（持久分区，`mtd_storage.sh save` 会写回
flash），重启不丢。

## menu.conf

UTF-8，一行一个菜单项：

```
广告过滤|Advanced_nanoad.asp|ad
运行日志|Advanced_nanoad_log.asp|ad
```

- 第 1 列：菜单标题（<=64 字节，按 UTF-8 字符边界截断）
- 第 2 列：`web/` 里的页面文件名（不含 `/`，不含 `..`）
- 第 3 列：分类（`ad/dns/proxy/tunnel/nas/net/term/campus/qos/misc`，可省略）

没有 `menu.conf` 时，httpd 依次找 `index.asp` → `index.html` → `index.htm`，
标题取 `manifest.json` 的 `title`，分类按包名猜。

## 访问地址

| 请求 | 实际文件 |
| --- | --- |
| `/plugin/<pkg>/<file>` | `/etc/storage/plugins/<pkg>/web/<file>` |
| `/plugin/<pkg>/css/a.css` | 允许子目录 |
| `/plugin/../etc/passwd` | 拒绝：包名白名单 `[A-Za-z0-9._-]`、禁止 `..`、禁止反斜杠 |

`.asp` 页面照常走 `do_ej()`（`<#tag#>` 翻译）和登录校验；`.html`/`.js`/`.css`/
图片按原样输出。插件页里的相对路径会落在 `/plugin/<pkg>/` 下。

## 插件仓库

插件包和索引放在同一仓库的 `plugin-repo` Release 上，路由侧只需要两个文件：

- `plugins.txt`：`name|title|file|size|sha256[|cat]`，awk 直接解析
- `<name>.tar.gz`：插件包本体

固件里默认下载地址：

```
https://github.com/jonepjxh/padavan-TurBoTse/releases/download/plugin-repo
```

可以用环境变量覆盖（自建反代 / 内网镜像）：

```sh
PLUGIN_REPO_MIRROR=http://192.168.1.10/plugin-repo pluginctl sync
```

下载走 `gh-proxy.com` → 原始地址 → `ghproxy.net` 三个候选，任一成功即返回；因为
GitHub release-assets 在部分网络下直连 443 会超时，镜像优先。

### 由源码交叉编译的第三方插件

`plugins/build-plugins.sh` + `.github/workflows/build-plugins.yml` 把 GitHub 上游源码交叉
编译成 mipsel-linux-musl 静态包（`CGO_ENABLED=0 GOARCH=mipsle GOMIPS=softfloat`，UPX 压缩），
push `plugins/**` 或手动触发工作流就会重新打包并刷新 `plugins.txt`。

| 插件 | 版本 | 分类 | 说明 |
| --- | --- | --- | --- |
| ddns-go | v6.10.0 | DNS服务 | 多服务商 DDNS，Web 管理页 `http://路由器:9876` |
| mosdns | master | DNS服务 | 本地 DNS 服务，默认 `0.0.0.0:5353`，配置在插件目录 `config.yaml` |
| trojan-go | master | 科学上网 | 需先把 `config.example.json` 复制到 `.home/trojan-go/config.json` 再启动 |
| speedtest-go | v1.8.0 | 网络工具 | 命令行测速：`speedtest-go --server --all`、`speedtest-go` |
| acme.sh | v3.1.7 | 其他插件 | 证书申请/续期，工作目录 `/etc/storage/plugins/.home/acme.sh` |

两个必须记住的坑：

1. **Go 版本必须锁 1.25，不能用 1.26**。go1.26 编出来的 mipsle 二进制在 K2P 的 4.4 内核上
   启动即崩（`runtime.futexwakeup ... returned -89` + SIGSEGV）；go1.23/1.25 编的同源二进制
   正常。脚本里是 `GOTOOLCHAIN=local` 配合 workflow 的 `setup-go 1.25`，所以上游模块如果
   把 `go.mod` 提到 1.26（ddns-go v6.18、gotty、speedtest-go master 都是这样），就必须钉一个
   旧 tag，否则构建直接失败。
2. **插件 payload 放 RAM**（`/tmp/plugins`，24MB tmpfs），128MB 内存的 K2P 同时跑多个 Go
   插件会很紧张：装太多会 `runtime: out of memory`，也不需要一次把清单里的都装上。

## 命令行

```sh
pluginctl sync              # 拉取插件清单
pluginctl list              # 输出 name|title|file|size|installed|enabled|cat
pluginctl install smartdns  # 下载并安装
pluginctl start smartdns    # 启用 / 跑 start.sh
pluginctl stop smartdns
pluginctl enable smartdns   # 写开机自启
pluginctl disable smartdns
pluginctl remove smartdns   # 卸载，菜单同步消失
pluginctl menu              # 当前会出现在菜单里的插件页
pluginctl setcfg smartdns cache=4096
```

## 新增一个插件

1. 编好二进制，放进插件包 `usr/bin` 或 `usr/sbin`；
2. 需要管理页面就放 `web/index.asp`（或 `web/menu.conf` 指定标题/分类）；
3. 打 tar.gz，写进 `plugins.txt`（有 `cat` 列最好），上传到 `plugin-repo` Release；
4. 路由器上「同步仓库」→「安装」。

不需要改 C 代码，也不需要重新编译固件。

## 排查

```sh
pluginctl status                      # 安装/启用状态
pluginctl menu                        # 菜单里应该出现什么
cat /etc/storage/plugins/.view.txt     # 插件中心页的数据源
logread | grep pluginctl              # 安装日志
```

菜单没出来时先确认 `state.js` 末尾有 `/* plugin center: dynamic menu (generated) */`
这段注入（浏览器强制刷新一次，`state.js` 是 no-cache 但浏览器可能仍缓存）。
