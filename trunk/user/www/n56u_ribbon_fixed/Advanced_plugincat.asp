<!DOCTYPE html>
<html>
<head>
<title><#Web_Title#> - 插件分类</title>
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

/* 分类目录页：/Advanced_plugincat.asp?c=<category>
 * 插件清单由 state.js 注入的 window.PLUGIN_MENU 提供（plugin_menu_emit 生成），
 * 本页只负责把某个分类下已安装的插件列出来，左侧菜单仍由 state.js 渲染。 */
/* 目录在左侧菜单里的位置由固件按已装插件动态分配（state.js 末尾注入的
 * window.PLUGIN_CAT_L2），下面的数值只在拿不到该表时兜底。 */
var CAT_L2 = { center: 11, ad: 12, dns: 13, proxy: 14, tunnel: 15, nas: 16, net: 17, term: 18, campus: 19, qos: 20, misc: 21 };
(function(){
	var m = window.PLUGIN_CAT_L2, k;
	if (!m) return;
	for (k in m) if (m.hasOwnProperty(k)) CAT_L2[k] = m[k];
})();
var CAT_NAME = { ad: "广告管理", dns: "DNS服务", proxy: "科学上网", tunnel: "穿透服务", nas: "存储下载", net: "网络工具", term: "终端运维", campus: "校园网认证", qos: "QOS流控", misc: "其他插件" };

function get_arg(name){
	var q = window.location.search.substring(1).split('&');
	for (var i = 0; i < q.length; i++){
		var p = q[i].split('=');
		if (p[0] === name) return decodeURIComponent(p[1] || '');
	}
	return '';
}

function initial(){
	var c = get_arg('c') || 'dns';
	try { show_banner(1); } catch(e) {}
	var l2 = CAT_L2[c];
	try { show_menu(5, (l2 > 0 ? l2 : 7), 99); } catch(e) {}
	try { show_footer(); } catch(e) {}
	render(c);
}

function esc(s){
	return String(s === undefined || s === null ? '' : s)
		.replace(/&/g, '&amp;').replace(/</g, '&lt;')
		.replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function render(c){
	var box = document.getElementById('catList');
	var items = (window.PLUGIN_MENU || []);
	var list = [], i;

	for (i = 0; i < items.length; i++){
		if (items[i] && items[i].c === c) list.push(items[i]);
	}
	document.getElementById('catTitle').innerHTML = (CAT_NAME[c] || '插件') + ' - 已安装插件';

	if (!list.length){
		box.innerHTML = '<div class="alert">该分类下暂无已安装插件。在「插件中心」里安装插件后刷新，即可在此看到入口。</div>';
		return;
	}

	var h = '<table class="table table-striped table-bordered" style="background:#fff;">'
		+ '<thead><tr><th style="width:45%">插件</th><th style="width:25%">目录</th>'
		+ '<th style="width:30%">操作</th></tr></thead><tbody>';
	for (i = 0; i < list.length; i++){
		var u = list[i].u || '';
		var own = (u.indexOf('/plugin/') === 0);   /* 插件自带页面 */
		var ops = '<a class="btn btn-small btn-primary" href="/Advanced_plugincfg.asp?p='
			+ encodeURIComponent(list[i].n) + '">管理 / 设置</a>';
		if (own)
			ops = '<a class="btn btn-small btn-primary" href="' + esc(u) + '">打开页面</a> ' + ops;
		h += '<tr><td><b>' + esc(list[i].t) + '</b></td>'
			+ '<td>' + esc(list[i].n) + '</td>'
			+ '<td>' + ops + '</td></tr>';
	}
	h += '</tbody></table>';
	box.innerHTML = h;
}
</script>
</head>

<body onLoad="initial();">

<div class="wrapper">
<div class="container-fluid" style="padding-right: 0px">
	<div class="row-fluid">
		<div class="span3"><center><div id="logo"></div></center></div>
		<div class="span9" >
			<div id="TopBanner"></div>
		</div>
	</div>
</div>

<div id="Loading" class="popup_bg"></div>

<div class="container-fluid">
	<div class="row-fluid">
		<div class="span3">
			<!--=====Beginning of Main Menu=====-->
			<div class="well sidebar-nav side_nav" style="padding: 0px;">
				<ul id="mainMenu" class="clearfix"></ul>
				<ul class="clearfix">
					<li>
						<div id="subMenu" class="accordion"></div>
					</li>
				</ul>
			</div>
		</div>

		<div class="span9">
			<!--Body content-->
			<div class="row-fluid">
				<div class="span12">
					<div class="box well grad_colour_dark_blue">
						<h2 class="box_head round_top"><span id="catTitle">插件分类</span></h2>
						<div class="round_bottom">
							<div class="row-fluid">
								<div id="tabMenu" class="submenuBlock"></div>
								<div id="catList" style="margin: 10px;"></div>
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
