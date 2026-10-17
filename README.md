# UU远程 Nintendo Switch 主控端

参考 `UURemote-web` 的已验证协议实现的原生 Switch 自制程序。Switch **独立连接网易 UU 正式 API、信令服务器和原版电脑端**，无需 Node 服务、浏览器、网页网关或额外的电脑代理。

当前交付为 **0.1.0 实验版**：已生成真实 ARM64 NRO，协议回归和本机原生 WebRTC 回环测试通过；尚未在 Switch 真机上验证登录和原版电脑串流，不能视为已经完成兼容性验证。

## 安装与使用

1. 从 [GitHub Releases](https://github.com/Antman2023/UURemote-ns/releases) 下载 `UURemote-ns-0.1.0-switch.zip`，解压到 SD 卡根目录，得到 `sd:/switch/UURemote-ns/UURemote-ns.nro`。
2. 在电脑上运行**原版 UU远程电脑端**并登录自己的账号。
3. Switch 联网，按住 R 启动一个游戏进入完整内存模式的 hbmenu，再打开 UU Remote NS。相册小程序模式会显示提示并退出。
4. 按 A 获取二维码，使用手机官方 UU远程 App 扫码并确认登录。
5. 选择账号下的在线电脑，按 A 进入桌面。当前默认请求 H.264、1280×720、30 FPS。

账号令牌和扫码凭据只保存在内存，重启程序需重新登录。SD 卡只保存随机生成的主控 `client-id`。连接时不会强制抢占已有远控会话。

## 操作

| 场景 | 按键 | 行为 |
| --- | --- | --- |
| 首页 | ↑ / ↓、A | 选择电脑、确认或扫码登录 |
| 首页 | Y、X | 刷新设备、退出账号 |
| 登录或连接中 | B / − | 取消 |
| 首页 | + | 退出程序 |
| 远程桌面 | 左摇杆 | 移动鼠标 |
| 远程桌面 | ZR / ZL | 鼠标左键 / 右键 |
| 远程桌面 | 触屏 | 绝对坐标移动、按住左键拖动；画面黑边不发送输入 |
| 远程桌面 | 右摇杆上下 | 鼠标滚轮 |
| 远程桌面 | A / B / X / Y | Enter / Esc / Space / Tab |
| 远程桌面 | 方向键、L / R | WASD、Ctrl / Shift |
| 远程桌面 | + / − | 显示操作说明 / 断开连接 |

这是键鼠映射，未实现 UU 原生虚拟 Xbox 手柄协议。系统休眠、失焦或断开连接时会尝试释放已按下的键鼠输入。

## 实现与限制

- C++17、devkitA64 / libnx；SDL2 显示和音频输出，系统共享字体显示中文。
- libcurl 直接发起签名 HTTPS 请求，TLS 校验保持开启。Socket.IO / Engine.IO 4 通过 TLS WebSocket 直连，带原版 `X-NRD-AUTH` 头。
- 保留 Web 版修正的 `04 || ConnectOptions` 二进制附件、嵌套 ICE 候选与五个原版数据通道。
- libdatachannel / libjuice / Mbed TLS / usrsctp / SRTP 负责原生 WebRTC。支持 IPv4 直连与服务端提供的 UDP TURN；不支持 TURN TCP/TLS。
- H.264 RTP 重组后交给 FFmpeg，优先尝试 NVTEGRA / NVDEC；不可用时使用软件解码。硬件帧先传到 CPU，再转换 RGBA 上传 SDL 纹理，尚未实现零拷贝显示。
- Opus 解码到 48 kHz 双声道 PCM，限制音频积压；尚未实现完整的抖动缓冲和音画时钟同步。
- 登录、设备列表、串流和键鼠输入均已接线；真实账号登录、NAT 穿透、NVDEC、性能和输入语义待真机联调。源码和构建成功不代表这些项目已实测通过。
- 目前提供扫码登录；短信登录、应用列表/启动 UI、剪贴板、文件传输、H.265、码率/分辨率设置和自动重连尚未实现。

## 从源码构建

依赖 CMake 3.21+、Ninja、Python 3.12+ 和 devkitPro Switch 工具链。标准 devkitPro 环境需安装 `switch-dev switch-sdl2 switch-sdl2_ttf switch-curl switch-ffmpeg switch-libopus`，其余 portlibs 由包依赖带入。

Windows 可直接使用项目内的独立工具链；新机器需 Python 3.14+ 解压锁定的 `.tar.zst` 包，不修改系统目录：

```powershell
python scripts/bootstrap_sdk.py
./scripts/build.ps1 -Apk 'C:\path\to\UURemote-TV-3.5.0.apk'
```

已有 devkitPro 时先设置 `$env:DEVKITPRO`。需要让 `cmake`、`ninja` 和 `python` 位于 PATH；项目存在 `.tools/ninja/ninja.exe` 时构建脚本优先使用它。输出为 `build-nx/UURemote-ns.nro` 与 `dist/` 安装包。

Linux / macOS：

```sh
export DEVKITPRO=/opt/devkitpro
python3 scripts/fetch_deps.py
python3 scripts/patch_deps.py
python3 scripts/extract_key.py /absolute/path/to/UU-TV-3.5.0.apk
cmake -S . -B build-nx -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-nx -j6
python3 scripts/package.py
```

第三方源码固定版本和 SHA256 见 `scripts/sources.lock.json`；Windows 工具链包固定 SHA256 见 `scripts/sdk-windows.lock.json`。依赖保存在被 Git 忽略的 `external/`，Switch 平台补丁由 `scripts/patch_deps.py` 幂等应用。

签名常量仅在本机构建时从用户提供的 TV 3.5.0 APK 提取，写入被 Git 忽略的 `romfs/signing.key` 并打包进 NRO；不打印、不写入源码。NRO 内嵌该材料，因此不应把它当作能够保密的客户端密钥。APK、UU 品牌与协议归原权利人所有；本项目为非官方客户端。

## 验证

协议测试可在普通 Windows / Linux / macOS 上运行，不需要 Switch：

```sh
cmake -S . -B build-protocol -G Ninja -DUU_CORE_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-protocol
ctest --test-dir build-protocol --output-on-failure
```

覆盖 Web 版固定签名与 Protobuf 字节对照、ACK、嵌套 ICE、信令主机限制，以及 300 组随机 WebSocket 长度与分片输入。

原生 WebRTC 回环检查：

```sh
cmake -S . -B build-rtc-test -G Ninja -DUU_RTC_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-rtc-test
ctest --test-dir build-rtc-test --output-on-failure
```

测试只使用本机两个原生对端，不连接真实 UU 账号，不构成原版电脑兼容性证明。详细实现位置与真机检查步骤见 [开发记录](docs/development.md)。
