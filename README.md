# UU Remote NS

**在 Nintendo Switch 上使用 UU 远程控制电脑。**

连接原版 UU 远程电脑端，接收桌面画面和声音，用 Joy-Con、触屏或屏幕键盘操作。UU Remote NS 是非官方 Switch Homebrew，目前为实验版。

[**下载最新版**](https://github.com/Antman2023/UURemote-ns/releases/latest) · [安装使用](#安装使用) · [按键操作](#按键操作) · [源码构建](#源码构建)

## 功能

- 扫码或短信登录，选择账号下的在线电脑。
- 通过 WebRTC 接收 H.264 视频与 Opus 音频，默认请求 720p / 30 FPS。视频优先尝试 NVDEC，无法初始化时使用软件解码。
- 使用摇杆移动鼠标、Joy-Con 操作常用按键，或用触屏拖动和点按屏幕键盘。

## 安装使用

准备一台能运行 Homebrew 的联网 Switch，并在电脑上登录原版 UU 远程客户端。

1. 在[最新版发布页](https://github.com/Antman2023/UURemote-ns/releases/latest)下载 `UURemote-ns-*-switch.zip`，解压到 SD 卡根目录。更新时先退出旧程序，再覆盖 `switch/UURemote-ns/UURemote-ns.nro`。
2. 在 Switch 上按住 **R** 启动一个游戏，进入完整内存模式的 hbmenu，打开 **UU Remote NS**。
3. 按 **A** 显示二维码，用手机 UU 远程 App 扫码并确认。也可按 **X** 切换短信登录，用方向键选择输入项，按 **A** 输入或确认。
4. 选择在线电脑，按 **A** 连接。串流中按 **−** 断开。

也可以下载[单独的 NRO](https://github.com/Antman2023/UURemote-ns/releases/latest/download/UURemote-ns.nro)，放入 SD 卡的 `switch/UURemote-ns/`，并用 [SHA256SUMS](https://github.com/Antman2023/UURemote-ns/releases/latest/download/SHA256SUMS) 校验。

## 按键操作

| 场景 | 按键 | 操作 |
| --- | --- | --- |
| 已登录首页 | **Y** / **X** / **+** | 刷新设备 / 退出账号 / 退出程序 |
| 登录或连接中 | **B** / **−** | 取消 |
| 远程桌面 | 左摇杆 | 移动鼠标 |
| 远程桌面 | **ZR** / **ZL** | 鼠标左键 / 右键 |
| 远程桌面 | 触屏 | 移动鼠标或按住拖动 |
| 远程桌面 | 右摇杆上下 | 滚轮 |
| 远程桌面 | 按下右摇杆 | 打开或关闭屏幕键盘 |
| 远程桌面 | **A** / **B** / **X** / **Y** | Enter / Esc / 空格 / Tab |
| 远程桌面 | **↑ ↓ ← →** / **L R** | W S A D / Ctrl Shift |
| 远程桌面 | **+** / **−** | 显示或隐藏操作提示 / 断开 |
| 屏幕键盘 | 方向键 / **A** / **B** | 选键 / 输入 / 关闭，也可触屏点按 |

## 故障排查

- **更新后仍显示旧版本**。核对首页版本号与发布页，并确认 hbmenu 打开的是覆盖后的 NRO。
- **真机或 Eden 花屏、绿屏或无画面**。在 SD 卡的 `switch/UURemote-ns/` 下创建无扩展名的空文件 `software-video`，重启客户端以使用软件解码。移除文件后恢复自动选择。
- **连接失败或崩溃**。反馈时附上客户端版本和 SD 卡上的 `switch/UURemote-ns/connection.log`。公开提交 Atmosphère 崩溃报告前，请删除可能包含凭据的内容。可以在 [GitHub Issues](https://github.com/Antman2023/UURemote-ns/issues) 反馈。

账号令牌保存在内存中，重启程序后需要重新登录。SD 卡保存客户端 ID 和连接阶段日志。

## 当前限制

- Joy-Con 映射为键鼠输入。虚拟手柄、剪贴板和文件传输尚未实现。
- 网络支持 IPv4 和 UDP TURN。TCP / TLS TURN 和自动重连尚未实现。
- 视频使用 H.264，当前没有分辨率或码率设置，也缺少完整的音画同步。

## 源码构建

需要 CMake 3.21+、Ninja、Python 3.12+ 和 devkitPro Switch 工具链。打包 NRO 还需要从自行提供的 UU TV 3.5.0 APK 中提取请求签名材料。

```sh
git clone https://github.com/Antman2023/UURemote-ns.git
cd UURemote-ns
```

### Windows

使用 Python 3.14+ 安装项目锁定的 SDK，并生成 NRO 和 SD 卡安装包。

```powershell
python scripts/bootstrap_sdk.py
./scripts/build.ps1 -Apk 'C:\path\to\UURemote-TV-3.5.0.apk'
```

已有 devkitPro 时，设置 `$env:DEVKITPRO` 后直接运行构建命令。

<details>
<summary>Linux / macOS</summary>

使用 devkitPro 安装 `switch-dev`、`switch-sdl2`、`switch-sdl2_ttf`、`switch-ffmpeg` 和 `switch-libopus`。

```sh
export DEVKITPRO=/opt/devkitpro
python3 scripts/fetch_deps.py
python3 scripts/patch_deps.py
python3 scripts/extract_key.py /absolute/path/to/UURemote-TV-3.5.0.apk
cmake -S . -B build-nx -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-nx -j6
python3 scripts/package.py
```

</details>

NRO 位于 `build-nx/UURemote-ns.nro`，安装包和 `SHA256SUMS` 位于 `dist/`。依赖版本与校验值见 [源码锁定清单](scripts/sources.lock.json) 和 [Windows SDK 锁定清单](scripts/sdk-windows.lock.json)。

### GitHub Actions

推送指向 `main` 上提交的 `vX.Y.Z` 标签会自动构建并发布为 **Latest**。标签须与 `CMakeLists.txt` 中的项目版本一致。面向 `main` 的 PR 会编译 ARM64 ELF。在 `main` 手动运行 **Build Switch** 并填写 `release_tag` 可以补发已有标签。

在自己的仓库打包时，将从 APK 提取的 `romfs/signing.key` 编码为 Base64，存到仓库的 Actions Secret `UU_SIGNING_KEY_BASE64`。生成的 NRO 会嵌入该签名材料。

技术结构、调试命令和真机排查步骤见[开发记录](docs/development.md)。

## 许可证与声明

项目采用 [GPL-3.0-or-later](LICENSE)，第三方授权见 [THIRD_PARTY.md](THIRD_PARTY.md)。UU 品牌与相关协议归原权利人所有。
