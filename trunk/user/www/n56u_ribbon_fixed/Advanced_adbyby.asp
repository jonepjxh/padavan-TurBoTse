<!DOCTYPE html>
<html>
<head>
<title><#Web_Title#> - <#menu5_20#></title>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta http-equiv="Pragma" content="no-cache">
<meta http-equiv="Expires" content="-1">

<link rel="shortcut icon" href="images/favicon.ico">
<link rel="icon" href="images/favicon.png">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/bootstrap.min.css">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/main.css">
<link rel="stylesheet" type="text/css" href="/bootstrap/css/engage.itoggle.css">

<script type="text/javascript" src="/jquery.js"></script>
<script type="text/javascript" src="/bootstrap/js/bootstrap.min.js"></script>
<script type="text/javascript" src="/bootstrap/js/engage.itoggle.min.js"></script>
<script type="text/javascript" src="/state.js"></script>
<script type="text/javascript" src="/general.js"></script>
<script type="text/javascript" src="/itoggle.js"></script>
<script type="text/javascript" src="/popup.js"></script>

<script>
var $j = jQuery.noConflict();
<% adbyby_status(); %>
<% login_state_hook(); %>

$j(document).ready(function(){
    init_itoggle('adbyby_enable');
});

function initial(){
    show_banner(2);
    show_menu(5,15);
    show_footer();
    fill_status(adbyby_status());
}

function fill_status(code){
    var text = (code == 1) ? "<#Running#>" : "<#Stopped#>";
    var cls = (code == 1) ? "success" : "warning";
    $j("#nanoad_status").html('<span class="label label-' + cls + '">' + text + '</span>');
}

function applyRule(){
    showLoading();
    document.form.action_mode.value = " Apply ";
    document.form.current_page.value = "Advanced_adbyby.asp";
    document.form.next_page.value = "";
    document.form.submit();
}

function serviceAction(action){
    showLoading();
    document.adbyby_action.connect_action.value = action;
    document.adbyby_action.submit();
}

function openWebUI(){
    window.open('http://' + location.hostname + ':18080/', '_blank');
}
</script>
</head>

<body onload="initial();">
<div id="TopBanner"></div>
<div id="Loading" class="popup_bg"></div>
<iframe name="hidden_frame" id="hidden_frame" src="" width="0" height="0" frameborder="0"></iframe>

<form method="post" name="form" id="ruleForm" action="/start_apply.htm" target="hidden_frame">
<input type="hidden" name="current_page" value="Advanced_adbyby.asp">
<input type="hidden" name="next_page" value="">
<input type="hidden" name="next_host" value="">
<input type="hidden" name="sid_list" value="">
<input type="hidden" name="action_mode" value="">
<input type="hidden" name="action_script" value="">

<table class="content" align="center" cellpadding="0" cellspacing="0" border="0">
<tr><td width="17">&nbsp;</td>
<td valign="top" width="202">
<div id="subMenu"></div></td>
<td valign="top">
<div id="TabContent">
<table width="760px" border="0" cellpadding="4" cellspacing="0" id="FormRow" class="FormTitle">
<tbody>
<tr>
<td height="30">&nbsp;</td>
<td><b><#menu5_20#> - NanoAd</b></td>
</tr>
</tbody>
</table>

<div id="wnd_nanoad">
<table width="760px" border="0" cellpadding="4" cellspacing="0" class="FormTitle" id="FormRow">
<tbody>
<tr>
<th width="35%">NanoAd 运行状态</th>
<td><span id="nanoad_status"></span></td>
</tr>
<tr>
<th width="35%">启用 NanoAd 广告过滤</th>
<td>
<div class="main_itoggle"><div id="adbyby_enable_on_of">
<input type="checkbox" id="adbyby_enable_fake" <% nvram_match_x("", "adbyby_enable", "1", "value=1 checked"); %><% nvram_match_x("", "adbyby_enable", "0", "value=0"); %>>
</div></div>
<div style="position:absolute; margin-left:-10000px;">
<input type="radio" value="1" name="adbyby_enable" id="adbyby_enable_1" <% nvram_match_x("", "adbyby_enable", "1", "checked"); %>><#checkbox_Yes#>
<input type="radio" value="0" name="adbyby_enable" id="adbyby_enable_0" <% nvram_match_x("", "adbyby_enable", "0", "checked"); %>><#checkbox_No#>
</div>
<div style="color:#888;">开启后由固件在 WAN 连接时自动启动；修改后请点击“应用”，再点击“重启服务”。</div>
</td>
</tr>
<tr>
<th width="35%">详细设置</th>
<td>
<input class="btn btn-primary" type="button" value="打开 NanoAd 管理页面" onclick="openWebUI();">
<div style="color:#888;">管理页仅监听局域网，端口 18080，默认账号 admin / 密码 admin。可在其中配置 DNS/SNI 过滤、订阅、黑白名单与域名测试。</div>
</td>
</tr>
</tbody>
</table>

<table class="table" width="760px">
<tr>
<td colspan="2">
<center>
<input class="btn btn-primary" style="width:150px" type="button" value="<#CTL_apply#>" onclick="applyRule();">&nbsp;
<input class="btn btn-success" style="width:150px" type="button" value="重启服务" onclick="serviceAction('restart');">&nbsp;
<input class="btn btn-info" style="width:150px" type="button" value="更新规则" onclick="serviceAction('updateadb');">
</center>
</td>
</tr>
</table>
</div>
</td></tr>
</table>
</div>
</td>
</tr>
</table>
</form>

<form method="post" name="adbyby_action" action="Ad_action.asp" target="_self">
<input type="hidden" name="connect_action" value="">
</form>

<div id="footer"></div>
</div>
</body>
</html>
