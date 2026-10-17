# 开发记录

## 结构

```text
Switch NRO
  ├─ libcurl HTTPS → 网易 UU 正式 API（签名、二维码、设备、加入房间）
  ├─ TLS WebSocket → 正式 Socket.IO 信令（自定义请求头）
  └─ libdatachannel ↔ 原版 UU 电脑端（ICE / DTLS / SRTP / SCTP）
       ├─ H.264 → FFmpeg NVDEC 或软件解码 → RGBA → SDL2
       ├─ Opus → PCM → SDL2 音频
       └─ Joy-Con / 触屏 → 原版 JSON 二进制控制数据
```

`source/protocol.cpp` 负责签名规范化、Protobuf、Socket.IO 数据解析和 WebSocket 帧；`source/transport.cpp` 负责签名 HTTPS 与使用 libcurl CONNECT_ONLY 的 TLS WebSocket。原版请求头、空头的 libcurl 分号语法、连接参数均以参考项目为依据。

`source/client.cpp` 在单个工作线程中处理 API、信令和用户命令。WebRTC 回调只向队列写入带连接代次的事件；断开时使旧代次失效。媒体回调使用独立的媒体代次检查，防止重连后收到旧画面。退出前关闭连接并等待 `rtc::Cleanup()`，然后释放网络服务。

`source/auth.cpp` 校验登录响应和短信参数，复用参考 Web 端的验证码发送与登录 API，成功发送后限制 60 秒内重发。`source/diagnostics.cpp` 仅将固定连接阶段名称写入 SD 卡日志，文件超过 128 KiB 后重建。

`source/media.cpp` 在独立解码线程上处理有界视频队列，使用 AVPacket 自有缓冲与 FFmpeg 所需 padding；显示线程只读取最新帧。音频积压超过 200 ms 时清空队列。这个初版没有完整的 RTP 重传策略、音频 jitter buffer 或音画同步；不能推定为稳定的游戏串流客户端。

`source/main.cpp` 提供 1280×720 手柄界面、中文系统字体、二维码和画面黑边坐标映射。要求 hbmenu 完整应用模式。布局不嵌入 WebView。

## 真机检查步骤

1. 以完整应用模式启动，检查系统字体、首页、返回与退出；未登录时不出现假设备。
2. 分别验证手机扫码和短信登录，确认账号和设备列表正确；检查切换登录方式、短信错误提示、60 秒重发、取消与二维码过期后重试。
3. 选择在线原版电脑，确认房间、控制权、ICE、DTLS 与画面；断开再连接至少三次。
4. 检查声音、黑边触控坐标、左右键、WASD、Ctrl/Shift、滚轮、组合键释放。
5. 在按住键鼠时按 Home、休眠或断开，确认电脑端没有残留按键。
6. 检查局域网与不同网络 UDP TURN；验证离线、被其他设备接管、掉线和账号失效提示。
7. 检查 NVDEC 是否实际被选中、720p30 稳态性能、内存与音画漂移；软件 fallback 的性能不能由编译结果保证。

这些步骤需要实际 Switch 和用户账号，本次开发环境无法替代。

## 本次验证结果

- Windows 上以 devkitA64 r30 / GCC 16.1.0、libnx 4.12.0 完成交叉编译与 NRO 打包。
- 本机协议测试通过：参考 Web 固定签名、连接 Protobuf、Socket.IO / ICE，以及 300 组随机 WebSocket 数据流。
- 本机原生 WebRTC 回环通过：ICE、DTLS、SCTP 二进制键盘消息、SRTP H.264 RTP 重组。
- 短信 API 参数、冷却时间、失败重试、前导零验证码和登录响应校验通过模拟请求测试；未发送真实短信。
- 使用实际 qrcodegen 编码器与新版布局生成 477×477 像素的合成登录二维码，经图像解码恢复出完整原始 URL；手机实扫仍需真机复测。
- Horizon 使用的 pthread 随机数分支通过 8 线程并发初始化、状态保留、线程隔离和退出清理测试。
- NRO0 与 ASET 资源段检查、ZIP CRC 和包内 NRO 一致性检查通过。
- 已收到用户真机连接阶段崩溃报告并完成符号定位；修复后的正式房间互通、短信实收、NVDEC、实际音视频与键鼠表现仍需真机复测。

## v0.1.0 连接崩溃定位

恢复发布版 ELF 后，其 Build ID `86ad8d2606b95ba1b9a6bbe9844a7acbde840f9a` 与用户报告完全一致。`2168-0002` 的 PC 偏移 `0xdc22c` 对应 `rtc::impl::IceTransport::IceTransport`，上层依次为 `initIceTransport`、`setLocalDescription` 和 `Client::beginPeer`。

故障指令位于 ICE 服务器随机排序前，访问模板函数中的 `thread_local` 随机数状态；该构建生成了错误的 `[thread_pointer + thread_pointer]` 地址。v0.1.1 在 Horizon 上使用 pthread key 保存每线程引擎，在线程退出时释放；其他平台保留原实现。已确认修复后的 ICE 目标文件调用 `pthread_getspecific` / `pthread_setspecific`，不再含原先的 `__aarch64_read_tp` 访问。补丁由 `scripts/patch_deps.py` 重复应用，不改动上游锁定版本。

每版排查应保存对应的 `build-nx/UURemote-ns.elf` 与 `.map`，并核对 Build ID。原始用户崩溃报告不纳入仓库或发布包。

## 参考

- 参考项目 `UURemote-web` 中的 `server/uu-api.mjs`、`public/lib/streamer.js`、`public/lib/protocol.js`、`public/lib/protobuf.js`。
- [libdatachannel](https://github.com/paullouisageneau/libdatachannel)：媒体接收与数据通道 API。
- [Switch SDL2 示例](https://github.com/switchbrew/switch-examples/tree/master/graphics/sdl2/sdl2-simple)：原生应用和依赖链接。
- [devkitPro FFmpeg 包构建](https://github.com/devkitPro/pacman-packages/blob/master/switch/ffmpeg/PKGBUILD)：Horizon/NVTEGRA 支持。
- [NVTEGRA FFmpeg 实现说明](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-May/328549.html)。
