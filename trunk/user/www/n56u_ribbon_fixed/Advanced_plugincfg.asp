<!DOCTYPE html>
<html>
<head>
<title><#Web_Title#> - 插件管理</title>
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

/* 通用插件管理页 /Advanced_plugincfg.asp?p=<pkg>
 *
 * 插件包内带 web/ 页面的，菜单直接指向 /plugin/<pkg>/<page>；没带页面的
 * （smartdns、ttyd、dogcom、3proxy 等纯二进制插件）统一指向本页，因此
 * 独立安装的插件和编进固件里的插件一样有管理入口：
 *   状态（已启用/未启用）、启动、停止、开机自启开关、
 *   以及插件自带 webui.conf 时的设置表单。
 * 数据由 web_ex.c 的 plugin_detail() / plugin_webui() 钩子注入。 */

var DETAIL = [];
var WEBUI  = [];
var PKG    = '';

function get_arg(name){
	var q = window.location.search.substring(1).split('&');
	for (var i = 0; i < q.length; i++){
		var p = q[i].split('=');
		if (p[0] === name) return decodeURIComponent(p[1] || '');
	}
	return '';
}

function esc(s){
	return String(s === undefined || s === null ? '' : s)
		.replace(/&/g, '&amp;').replace(/</g, '&lt;')
		.replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function findPkg(n){
	for (var i = 0; i < DETAIL.length; i++)
		if (DETAIL[i] && DETAIL[i].n === n) return DETAIL[i];
	return null;
}

function findUi(n){
	for (var i = 0; i < WEBUI.length; i++)
		if (WEBUI[i] && WEBUI[i].n === n) return WEBUI[i];
	return null;
}

function stateText(p){
	if (p.e === 1) return '<span style="color:#3a8;font-weight:bold;">已启用</span>';
	if (p.e === 0) return '<span style="color:#c60;">已禁用</span>';
	return '<span style="color:#999;">未知</span>';
}

function initial(){
	try { show_banner(1); } catch(e) {}

	PKG = get_arg('p');
	/* 只接受合法的插件目录名，避免把 URL 参数直接拼进 shell 命令 */
	if (PKG && !/^[A-Za-z0-9._-]+$/.test(PKG)) PKG = '';

	var l2 = 7;
	var info = PKG ? findPkg(PKG) : null;
	if (info && info.c && window.PLUGIN_CAT_L2 && window.PLUGIN_CAT_L2[info.c])
		l2 = window.PLUGIN_CAT_L2[info.c];
	try { show_menu(5, l2, 99); } catch(e) {}
	try { show_footer(); } catch(e) {}

	if (PKG) renderOne(); else renderAll();
	hideLoading();
}

/* ---------------- 全部插件列表 ---------------- */
function renderAll(){
	var box = document.getElementById('cfgBody');
	document.getElementById('cfgTitle').innerHTML = '已安装插件';

	if (!DETAIL.length){
		box.innerHTML = '<div class="alert">还没有安装任何插件。请在「插件中心」里同步并安装。</div>';
		return;
	}

	var h = '<table class="table table-striped table-bordered" style="background:#fff;">'
		+ '<thead><tr><th style="width:30%">插件</th><th style="width:20%">分类</th>'
		+ '<th style="width:20%">状态</th><th style="width:30%">操作</th></tr></thead><tbody>';
	for (var i = 0; i < DETAIL.length; i++){
		var p = DETAIL[i];
		h += '<tr><td><b>' + esc(p.t) + '</b><br><small>' + esc(p.n) + '</small></td>'
			+ '<td>' + esc(p.g) + (p.h ? '' : '<br><small style="color:#999;">无自带页面</small>') + '</td>'
			+ '<td>' + stateText(p) + '</td>'
			+ '<td><a class="btn btn-small btn-primary" href="/Advanced_plugincfg.asp?p='
			+ encodeURIComponent(p.n) + '">管理</a></td></tr>';
	}
	h += '</tbody></table>';
	box.innerHTML = h;
}

/* ---------------- 单个插件 ---------------- */
function renderOne(){
	var box = document.getElementById('cfgBody');
	var p = findPkg(PKG);

	if (!p){
		document.getElementById('cfgTitle').innerHTML = '插件管理';
		box.innerHTML = '<div class="alert">插件 ' + esc(PKG) + ' 未安装。</div>';
		return;
	}
	document.getElementById('cfgTitle').innerHTML = esc(p.t) + ' 管理';

	var h = '<table class="table table-bordered" style="background:#fff;">';
	h += '<tr><th style="width:25%">插件名</th><td>' + esc(p.t) + ' <small style="color:#999;">(' + esc(p.n) + ')</small></td></tr>';
	h += '<tr><th>分类</th><td>' + esc(p.g) + '</td></tr>';
	h += '<tr><th>状态</th><td>' + stateText(p) + '</td></tr>';
	h += '<tr><th>管理方式</th><td>'
		+ (p.h ? '插件自带页面 <a class="btn btn-small btn-primary" href="' + esc(p.u) + '">打开</a>'
		       : '插件没有自带 web 页面，使用本页启停与设置')
		+ '</td></tr>';
	h += '</table>';

	h += '<div style="margin:12px 0;">'
		+ '<button class="btn btn-small btn-success" onclick="doCmd(\'start\')">启动 / 启用</button> '
		+ '<button class="btn btn-small" onclick="doCmd(\'stop\')">停止</button> '
		+ '<button class="btn btn-small" onclick="doCmd(\'enable\')">开启开机自启</button> '
		+ '<button class="btn btn-small" onclick="doCmd(\'disable\')">关闭开机自启</button> '
		+ '<button class="btn btn-small" onclick="location.href=\'/Advanced_plugincfg.asp?p='
		+ encodeURIComponent(PKG) + '&r=\'+(new Date()).getTime()">刷新</button>'
		+ '</div>';

	h += '<div id="cfgForm"></div>';
	box.innerHTML = h;

	renderForm(p);
}

function renderForm(p){
	var box = document.getElementById('cfgForm');
	var ui = findUi(p.n);

	if (!ui || !ui.f || !ui.f.length){
		box.innerHTML = '<div class="alert">该插件没有提供可配置项（没有 webui.conf）。'
			+ '如需修改参数，可直接编辑 <code>/etc/storage/plugins/' + esc(p.n) + '/config</code>。</div>';
		return;
	}

	var h = '<h4 style="margin-top:18px;">插件设置</h4>'
		+ '<table class="table table-bordered" style="background:#fff;"><tbody>';
	for (var i = 0; i < ui.f.length; i++){
		var f = ui.f[i];               /* [key, label, type, def, cur] */
		var id = 'cfg_' + f[0];
		h += '<tr><th style="width:35%">' + esc(f[1]) + '</th><td>';
		if (f[2] === 'radio' || f[2] === 'bool'){
			h += '<select id="' + esc(id) + '" class="input-medium">'
				+ '<option value="1"' + (String(f[4]) === '1' ? ' selected' : '') + '>启用</option>'
				+ '<option value="0"' + (String(f[4]) !== '1' ? ' selected' : '') + '>禁用</option>'
				+ '</select>';
		} else {
			h += '<input type="text" id="' + esc(id) + '" class="input-xxlarge" value="' + esc(f[4]) + '">';
		}
		if (f[0] === 'x') {} /* placeholder */
		h += '<div><small style="color:#999;">键名 ' + esc(f[0]) + '，默认 ' + esc(f[3]) + '</small></div>';
		h += '</td></tr>';
	}
	h += '</tbody></table>'
		+ '<div style="margin:8px 0 16px;">'
		+ '<button class="btn btn-primary" onclick="saveCfg()">保存设置</button> '
		+ '<button class="btn" onclick="saveCfg(1)">保存并重启插件</button>'
		+ '</div>';
	box.innerHTML = h;
}

/* 单引号包裹，内部单引号转义，防止值里的特殊字符破坏 shell 命令 */
function shq(s){
	return "'" + String(s === null || s === undefined ? '' : s).replace(/'/g, "'\\''") + "'";
}

function saveCfg(restart){
	var ui = findUi(PKG);
	if (!ui || !ui.f) return;
	var args = [];
	for (var i = 0; i < ui.f.length; i++){
		var el = document.getElementById('cfg_' + ui.f[i][0]);
		if (!el) continue;
		args.push(shq(ui.f[i][0] + '=' + el.value));
	}
	if (!args.length) return;

	var cmd = 'sh /usr/bin/pluginctl setcfg ' + shq(PKG) + ' ' + args.join(' ');
	if (restart)
		cmd += ';sh /usr/bin/pluginctl stop ' + shq(PKG) + ' >/dev/null 2>&1; sleep 1; sh /usr/bin/pluginctl start ' + shq(PKG) + ' >/dev/null 2>&1';

	try { showLoading(); } catch(e) {}
	postSystemCmd(cmd);
}

function doCmd(action){
	var map = {
		start:   'sh /usr/bin/pluginctl start ',
		stop:    'sh /usr/bin/pluginctl stop ',
		enable:  'sh /usr/bin/pluginctl enable ',
		disable: 'sh /usr/bin/pluginctl disable '
	};
	if (!map[action]) return;
	try { showLoading(); } catch(e) {}
	postSystemCmd(map[action] + shq(PKG));
}

/* SystemCmd 必须走 /apply.cgi，再由 body 里的 nvram_dump 钩子触发 syscmd.sh */
function postSystemCmd(syscmd){
	var url = '/Advanced_plugincfg.asp?p=' + encodeURIComponent(PKG);
	var body = 'action_mode=' + encodeURIComponent(' SystemCmd ')
		+ '&SystemCmd=' + encodeURIComponent(syscmd)
		+ '&sid_list=' + encodeURIComponent('General.')
		+ '&current_page=' + encodeURIComponent(url)
		+ '&next_page=' + encodeURIComponent(url);
	var x = new XMLHttpRequest();
	x.open('POST', '/apply.cgi', true);
	x.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
	x.onreadystatechange = function(){
		if (x.readyState !== 4) return;
		location.href = url + '&r=' + (new Date()).getTime();
	};
	x.send(body);
}

function done_validating(action){ refreshpage(); }
</script>
</head>

<body onload="initial();" onunLoad="return unload_body();">
<!-- 命令执行钩子：页面渲染时 syscmd.sh 会执行 /apply.cgi 存入的 SystemCmd -->
<textarea id="syscmd_log" style="display:none" readonly><% nvram_dump("syscmd.log","syscmd.sh"); %></textarea>
<script type="text/javascript">
DETAIL = <% plugin_detail(); %>;
WEBUI  = <% plugin_webui(); %>;
</script>

<div class="wrapper">
<div class="container-fluid" style="padding-right: 0px">
	<div class="row-fluid">
		<div class="span3"><center><div id="logo"></div></center></div>
		<div class="span9" ><div id="TopBanner"></div></div>
	</div>
</div>

<div id="Loading" class="popup_bg"></div>

<div class="container-fluid">
	<div class="row-fluid">
		<div class="span3">
			<div class="well sidebar-nav side_nav" style="padding: 0px;">
				<ul id="mainMenu" class="clearfix"></ul>
				<ul class="clearfix">
					<li><div id="subMenu" class="accordion"></div></li>
				</ul>
			</div>
		</div>

		<div class="span9">
			<div class="row-fluid">
				<div class="span12">
					<div class="box well grad_colour_dark_blue">
						<h2 class="box_head round_top"><span id="cfgTitle">插件管理</span></h2>
						<div class="round_bottom">
							<div class="row-fluid">
								<div id="tabMenu" class="submenuBlock"></div>
								<div id="cfgBody" style="margin: 10px;"></div>
							</div>
						</div>
					</div>
				</div>
			</div>
		</div>
	</div>
</div>

<div id="footer"></div>
</div>

</body>
</html>
