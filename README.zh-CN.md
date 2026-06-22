# ShareThisFolder

[English](README.md)

ShareThisFolder 是一个轻量级 Windows 文件夹共享工具，快速将当前文件夹分享给手机和其他设备。

## 新功能

- **UPnP 端口映射** - 请求公网访问时优先尝试路由器 UPnP 映射。
- **STUN 隧道 fallback** - 为兼容的 NAT 环境增加 STUN 公网端点发现和本地转发。
- **资源管理器地址栏快捷唤起** - 在设置中启用快捷唤起后，可以在 Windows 资源管理器地址栏输入 `stf` 来共享当前文件夹。
## 功能特性

- **一键局域网共享** - 在要共享的文件夹中运行 exe，即可通过局域网分享该目录。
- **适配手机浏览器** - 提供简洁的移动端目录浏览和文件下载页面。
- **双语界面** - 自动识别系统语言，多语言支持。
- **单文件可执行程序** - 只需发起共享方下载可执行程序。
- **快捷唤起** - 可在设置菜单中安装系统级 `stf` 命令。
- **公网访问** - 最大支持在NAT3网络环境下内网穿透。

## 快速开始

### 下载

从 [Releases](https://github.com/lnblxj/ShareThisFolder/releases) 下载最新版本。

### 从源码构建

需要安装 Visual Studio 2022，并启用 C++ 桌面开发工作负载。

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

生成的程序位于 `build/Release/ShareThisFolder.exe`。

## 使用方式

### 共享当前文件夹

1. 将 `ShareThisFolder.exe` 放到想要共享的文件夹中。
2. 双击运行，或在终端中运行。
3. 在手机或其他设备上打开菜单中显示的 LAN 地址。
4. 按地址序号可复制对应 URL。
5. 按 `s` 可选择地址并显示二维码。
6. 按 `q` 或 `Ctrl+C` 停止共享。

### 开启公网访问

在主菜单按 `w`。程序会尝试打通NAT隧道，打洞成功，菜单中会新增一个 `WAN` 地址。

公网访问是否可用取决于路由器、运营商、防火墙和 NAT 类型。推荐路由器开启UPnP 映射提升打洞成功率。

### 设置资源管理器快捷唤起

1. 按 `e` 打开设置。
2. 选择 `设置快捷唤起`。
3. 同意 Windows 管理员权限提示。
4. 程序会复制自身到 `C:\Program Files\ShareThisFolder\stf.exe`，并将该目录加入系统 `Path`。
5. 在 Windows 资源管理器中打开任意文件夹，在地址栏输入 `stf` 并回车，即可共享当前文件夹。

如需移除，在设置菜单中选择 `移除快捷唤起`。

## 许可证

MIT
