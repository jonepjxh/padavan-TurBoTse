<!DOCTYPE html>
<html>
<head>
<title><#Web_Title#> - <#menu5_40#></title>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta http-equiv="Pragma" content="no-cache">
<meta http-equiv="Expires" content="-1">
<link rel="shortcut icon" href="images/favicon.ico">
<link rel="icon" href="images/favicon.png">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/bootstrap.min.css">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/main.css">
<script type="text/javascript" src="/jquery.js"></script>
<script type="text/javascript" src="/state.js"></script>
<script type="text/javascript" src="/general.js"></script>
<script type="text/javascript" src="/popup.js"></script>
<script>
<% login_state_hook(); %>

/* 插件中心：在线下载 / 安装 / 卸载 / 启停。
 * 插件清单由 <% plugin_center(); %> 注入（pluginctl list 的输出），
 * 已装插件的菜单由 state.js 末尾的注入脚本动态生成，安装后刷新页面即可看到。 */
var plugins = [];
var raw = [];

/* 分类目录：顺序即显示顺序，和固件 web_ex.c 里的 plugin_cats[] 保持一致 */
var CATS = [["ad","广告管理"],["dns","DNS服务"],["proxy","科学上网"],["tunnel","穿透服务"],
            ["nas","存储下载"],["net","网络工具"],["term","终端运维"],["campus","校园网认证"],
            ["qos","QOS流控"],["misc","其他插件"]];
var CAT_WORDS = {
    ad:     ["adbyby","adguard","adblock","nanoad","adg"],
    dns:    ["smartdns","dnsmasq","mosdns","dnscrypt","stubby","chinadns","dns2tcp","dnsproxy","dnsforwarder","pdnsd"],
    proxy:  ["xray","v2ray","clash","trojan","shadowsocks","ssr","singbox","sing-box","redsocks","microsocks","srelay","ipt2socks","3proxy","obfs"],
    tunnel: ["frp","zerotier","ddnsto","tailscale","wireguard","aliddns","nps","ngrok","ptunnel"],
    nas:    ["alist","aliyun","aldriver","webdav","aria2","transmission","xupnpd","vsftpd","samba","ftp"],
    net:    ["iperf","socat","mtr","tcpdump","lrzsz","resolveip","lldt","msd_lite","nmap","inadyn","wol"],
    term:   ["ttyd","htop","nano","vim","shellinabox","webssh","telnet"],
    campus: ["mentohust","u2ec","scutclient","dogcom","drcom","minieap","gdut"],
    qos:    ["qos","sqm","cake"]
};
var curCat = "all";
var onlyInstalled = false;
var keyword = "";

function catOf(name, hint){
    var n = String(name || "").toLowerCase(), i, j;
    if (hint) for (i = 0; i < CATS.length; i++) if (CATS[i][0] === hint) return hint;
    for (i = 0; i < CATS.length; i++){
        if (!CAT_WORDS[CATS[i][0]]) continue;
        for (j = 0; j < CAT_WORDS[CATS[i][0]].length; j++)
            if (n.indexOf(CAT_WORDS[CATS[i][0]][j]) >= 0) return CATS[i][0];
    }
    return "misc";
}
function catName(key){
    for (var i = 0; i < CATS.length; i++) if (CATS[i][0] === key) return CATS[i][1];
    return "其他插件";
}
/* 已装插件的真实菜单地址（state.js 注入的 window.PLUGIN_MENU） */
function menuUrlOf(name){
    var m = window.PLUGIN_MENU || [];
    for (var i = 0; i < m.length; i++) if (m[i] && m[i].n === name) return m[i].u;
    return "";
}

function initial(){
    parseData();
    render();
    try { show_banner(1); } catch(e) {}
    var centerL2 = (window.PLUGIN_CAT_L2 && window.PLUGIN_CAT_L2.center) ? window.PLUGIN_CAT_L2.center : 11;
    /* 本目录没有子标签页，L3 传 0 让 show_menu 清空 tab 栏 */
    try { show_menu(5, centerL2, 0); } catch(e) {}
    try { show_footer(); } catch(e) {}
}

function parseData(){
    plugins = [];
    for (var i = 0; i < raw.length; i++){
        var p = raw[i].split('|');
        if (p.length < 6) continue;
        plugins.push({
            name: p[0], title: p[1], file: p[2], size: p[3],
            installed: p[4] === '1', enabled: p[5] === '1',
            cat: catOf(p[0], p.length > 6 ? p[6] : '')
        });
    }
}

function fmtSize(n){
    n = parseInt(n) || 0;
    if (n >= 1048576) return (n/1048576).toFixed(1) + ' MB';
    if (n >= 1024) return (n/1024).toFixed(0) + ' KB';
    return n + ' B';
}

function chips(){
    var h = '', i, cnt;
    var keys = ["all"].concat(CATS.map(function(c){ return c[0]; }));
    for (i = 0; i < keys.length; i++){
        var k = keys[i];
        cnt = 0;
        for (var j = 0; j < plugins.length; j++){
            if (k === "all" || plugins[j].cat === k){
                if (onlyInstalled && !plugins[j].installed) continue;
                cnt++;
            }
        }
        if (k !== "all" && !cnt) continue;
        h += '<a href="javascript:void(0)" onclick="setCat(\'' + k + '\')" class="btn btn-mini" '
          +  'style="margin:2px 4px 2px 0;' + (curCat === k ? 'background:#0a7bd4;color:#fff;' : '') + '">'
          +  (k === "all" ? "全部" : catName(k)) + ' (' + cnt + ')</a>';
    }
    return h;
}

function setCat(k){ curCat = k; render(); }
function toggleInstalled(){ onlyInstalled = !onlyInstalled; render(); }
function onSearch(v){ keyword = String(v || "").toLowerCase(); render(); }

function render(){
    var box = document.getElementById('pluginList');
    if (!box) return;
    var bar = document.getElementById('catBar');
    if (bar) bar.innerHTML = chips();

    if (!plugins.length){
        box.innerHTML = '<div class="alert">插件列表为空。首次使用请点击「同步仓库」从 GitHub 拉取插件清单（需要路由器能访问 GitHub）。</div>';
        return;
    }

    var list = [], i;
    for (i = 0; i < plugins.length; i++){
        var p = plugins[i];
        if (curCat !== "all" && p.cat !== curCat) continue;
        if (onlyInstalled && !p.installed) continue;
        if (keyword && (p.name + ' ' + p.title).toLowerCase().indexOf(keyword) < 0) continue;
        list.push(p);
    }
    if (!list.length){
        box.innerHTML = '<div class="alert">当前筛选条件下没有插件。</div>';
        return;
    }

    var h = '<table class="table table-striped table-bordered" style="background:#fff;">'
          + '<thead><tr><th style="width:34%">插件</th><th style="width:13%">分类</th>'
          + '<th style="width:10%">大小</th><th style="width:17%">状态</th>'
          + '<th style="width:26%">操作</th></tr></thead><tbody>';
    for (i = 0; i < list.length; i++){
        var p = list[i];
        var state = p.installed
            ? (p.enabled ? '<span style="color:#2a8a4a;font-weight:bold;">已安装 · 已启用</span>'
                         : '<span style="color:#c60;">已安装 · 未启用</span>')
            : '<span style="color:#999;">未安装</span>';
        var ops;
        if (!p.installed){
            ops = '<button class="btn btn-mini btn-primary" onclick="doCmd(\'install ' + p.name + '\')">安装</button>';
        } else {
            var u = menuUrlOf(p.name);
            ops = (u ? '<a class="btn btn-mini" href="' + u + '">打开页面</a> ' : '')
                + '<button class="btn btn-mini" onclick="doCmd(\'start ' + p.name + '\')">启用</button> '
                + '<button class="btn btn-mini" onclick="doCmd(\'stop ' + p.name + '\')">停用</button> '
                + '<button class="btn btn-mini btn-danger" onclick="doCmd(\'remove ' + p.name + '\')">卸载</button>';
        }
        h += '<tr><td><b>' + p.title + '</b><br><small style="color:#888;">' + p.name + '</small></td>'
          +  '<td><small>' + catName(p.cat) + '</small></td>'
          +  '<td>' + fmtSize(p.size) + '</td>'
          +  '<td>' + state + '</td><td>' + ops + '</td></tr>';
    }
    h += '</tbody></table>';
    box.innerHTML = h;
}

function doCmd(cmd){
    if (cmd.indexOf('remove') === 0 && !confirm('确定卸载该插件？卸载后左侧菜单里的入口会一起消失。')) return;
    showBusy(cmd.indexOf('install') === 0 ? '正在下载并安装，请稍候（大插件可能要几十秒）...' : '正在执行...');
    postSystemCmd("sh /usr/bin/pluginctl " + cmd + " >/dev/null 2>&1; sh /usr/bin/pluginctl list >/dev/null 2>&1");
}
function doSync(){
    showBusy('正在从 GitHub 同步插件清单...');
    postSystemCmd("sh /usr/bin/pluginctl sync >/dev/null 2>&1");
}
function goList(){
    postSystemCmd("sh /usr/bin/pluginctl list >/dev/null 2>&1");
}
function showBusy(msg){
    var b = document.getElementById('busy');
    if (b){ b.innerHTML = msg; b.style.display = 'block'; }
    try { showLoading(); } catch(e) {}
}

/* 命令提交到 /apply.cgi（apply_cgi 的 action_mode==" SystemCmd " 分支），
   再由本页 body 里的 nvram_dump("syscmd.log","syscmd.sh") 钩子执行。 */
function postSystemCmd(syscmd){
    var body = 'action_mode=' + encodeURIComponent(' SystemCmd ')
             + '&SystemCmd=' + encodeURIComponent(syscmd)
             + '&sid_list=' + encodeURIComponent('General.')
             + '&current_page=' + encodeURIComponent('/Advanced_plugincenter.asp')
             + '&next_page=' + encodeURIComponent('/Advanced_plugincenter.asp');
    var x = new XMLHttpRequest();
    x.open('POST', '/apply.cgi', true);
    x.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    x.onreadystatechange = function(){
        if (x.readyState !== 4) return;
        location.href = '/Advanced_plugincenter.asp?' + (new Date()).getTime();
    };
    x.send(body);
}
function done_validating(action){ refreshpage(); }
</script>
</head>

<body onload="initial();" onunLoad="return unload_body();">
<textarea id="syscmd_log" style="display:none" readonly><% nvram_dump("syscmd.log","syscmd.sh"); %></textarea>
<script type="text/javascript">raw = <% plugin_center(); %>;</script>
<div class="wrapper">
<div class="container-fluid" style="padding-right:0px">
<div class="row-fluid"><div class="span3"><center><div id="logo"></div></center></div><div class="span9"><div id="TopBanner"></div></div></div>
</div>
<div id="Loading" class="popup_bg"></div>
<iframe name="hidden_frame" id="hidden_frame" src="" width="0" height="0" frameborder="0"></iframe>
<form method="post" name="form" id="ruleForm" action="/apply.cgi" target="hidden_frame">
<input type="hidden" name="current_page" value="Advanced_plugincenter.asp">
<input type="hidden" name="next_page" value="">
<input type="hidden" name="next_host" value="">
<input type="hidden" name="sid_list" value="General.">
<input type="hidden" name="group_id" value="">
<input type="hidden" name="action_mode" value="">
<input type="hidden" name="action_script" value="">
<input type="hidden" name="SystemCmd" value="">

<div class="container-fluid">
<div class="row-fluid"><div class="span12">
<div class="well" style="background:#fff;">
<h3 style="margin-top:0;"><#menu5_40#> <small>在线下载 · 安装 · 卸载插件</small></h3>
<p style="color:#666;margin-bottom:8px;">
插件包由本仓库的 GitHub Release 提供，安装到路由器持久化分区 <code>/etc/storage/plugins/</code>，重启不丢失。
安装后插件会自动归入左侧「系统日志」之后的对应分类目录（广告管理 / DNS服务 / 科学上网 / 穿透服务 / 存储下载 / 网络工具 / 终端运维 / 校园网认证 / QOS流控 / 其他插件）。
</p>
<div style="margin-bottom:10px;">
<button class="btn btn-success" onclick="doSync()">同步仓库</button>
<button class="btn" onclick="goList()">刷新列表</button>
<button class="btn" onclick="toggleInstalled()" id="btnOnly">只看已安装</button>
<input type="text" placeholder="搜索插件" onkeyup="onSearch(this.value)" style="margin-left:8px;height:26px;">
</div>
<div id="busy" style="display:none;margin-bottom:10px;padding:8px;background:#eef6ff;border:1px solid #b9d8f5;"></div>
<div id="catBar" style="margin-bottom:8px;"></div>
<div id="pluginList"></div>
</div>
</div></div>
</div>
</form>
</div>
</body>
</html>
