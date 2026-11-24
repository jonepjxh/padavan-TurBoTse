# zzdemo - plugin center demo

Smallest possible plugin that proves the dynamic menu works:

```sh
tar -czf zzdemo.tar.gz -C zzdemo .      # or: make a .tar.gz with install.sh + web/
```

Then on the router (plugin center -> 插件仓库 -> 安装, or by hand):

```sh
tar -xzf zzdemo.tar.gz -C /etc/storage/plugins/zzdemo
pluginctl menu            # zzdemo  <TAB>  插件示例  <TAB>  /plugin/zzdemo/Advanced_zzdemo.asp
```

Reload any router page: **高级设置 → 插件示例** is now in the left menu.
Delete the directory and the entry disappears again.

## 写插件页面的两个坑

1. **不要用 `<#...#>` 词条标记**。这类占位符只有在固件词典（CN.dict / EN.dict）里存在对应
   词条时才会被替换，插件自带的词条不会被编进固件词典，页面会原样显示 `<#XXX#>`。
   插件页请直接写中文/英文文本（页面 `<meta charset="utf-8">`）。
2. **静态资源用相对路径**。页面地址是 `/plugin/<name>/<page>`，同目录下的 `.css` / `.js` /
   图片按相对路径引用即可，httpd 同样走插件路由。

## 菜单分类（把插件放进「高级设置」下的对应目录）

`web/menu.conf` 每行 `标题|页面文件[|分类]`，第三列是可选的分类标号，留空即落在「高级设置」本体：

| 分类 | 目录（排在系统日志之后） | 常见包名关键字（不写分类时按包名猜） |
| --- | --- | --- |
| `ad` | 广告管理 | adbyby, adguard, adblock, nanoad |
| `dns` | DNS服务 | dns, mosdns, dnscrypt, stubby |
| `tunnel` | 穿透服务 | frp, zerotier, ddns-to, tailscale, wireguard |
| `proxy` | 科学上网 | xray, v2ray, clash, trojan, shadowsocks |
| `nas` | 阿里云盘 | alist, aliyun, aldriver, webdav |
| `qos` | QOS流控 | qos, sqm, cake |

例：`NanoAd 广告过滤|index.html|ad`。分类目录只在该分类下确有已安装插件时才出现，
点进去是分类页 `/Advanced_plugincat.asp?c=<分类>`，列出该分类下所有已装插件。
固件本身已占用的同名目录（例如自带 adbyby）不会被覆盖，插件条目只是并入该目录。
