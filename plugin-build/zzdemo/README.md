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
