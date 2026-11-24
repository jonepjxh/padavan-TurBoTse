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
