<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title><#PluginDemo#></title>
<style>
body { font-family: Arial, "Microsoft YaHei", sans-serif; font-size: 15px; margin: 14px; background: #f5f7fa; color: #1f2d3d; }
h2 { font-size: 19px; margin: 0 0 12px; }
.box { background: #fff; border: 1px solid #dde3ea; border-radius: 8px; padding: 14px 16px; margin-bottom: 12px; }
table { border-collapse: collapse; width: 100%; }
td { padding: 6px 8px; border-bottom: 1px solid #eef1f5; vertical-align: top; }
td.k { width: 150px; color: #5a6b7f; }
code { background: #eef3f8; padding: 1px 5px; border-radius: 4px; }
.tip { color: #2b6cb0; background: #e8f1fb; border: 1px solid #bcd8f5; border-radius: 8px; padding: 10px 12px; }
</style>
</head>
<body>

<h2><#PluginDemo#></h2>

<div class="box">
<table>
<tr><td class="k">插件目录</td><td><code>/etc/storage/plugins/zzdemo</code></td></tr>
<tr><td class="k">本页地址</td><td><code>/plugin/zzdemo/Advanced_zzdemo.asp</code></td></tr>
<tr><td class="k">菜单来源</td><td><code>web/menu.conf</code> → 插件示例|Advanced_zzdemo.asp</td></tr>
<tr><td class="k">静态资源</td><td>同目录下的 .css / .js / 图片可直接用相对路径引用</td></tr>
</table>
</div>

<div class="tip">
本页由插件自带，安装在 <b>/etc/storage/plugins</b> 持久分区，固件重刷前一直有效；<br>
在插件中心卸载该插件后，这里的菜单项会自动消失。
</div>

</body>
</html>
