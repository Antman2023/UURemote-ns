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

`source/media.cpp` 在独立解码线程上处理有界视频队列，使用 AVPacket 自有缓冲与 FFmpeg 所需 padding；显示线程只读取最新帧。音频积压超过 200 ms 时清空队列。这个初版没有完整的 RTP 重传策略、音频 jitter buffer 或音画同步；不能推定为稳定的游戏串流客户端。

`source/main.cpp` 提供 1280×720 手柄界面、中文系统字体、二维码和画面黑边坐标映射。要求 hbmenu 完整应用模式。布局不嵌入 WebView。

## 真机检查步骤

1. 以完整应用模式启动，检查系统字体、首页、返回与退出；未登录时不出现假设备。
2. 使用自己的手机扫码，确认账号和设备列表正确；取消或二维码过期后可以重试。
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
- NRO0 与 ASET 资源段检查、ZIP CRC 和包内 NRO 一致性检查通过。
- 未验证 Switch 实机运行、原版 UU 账号授权、正式房间互通、NVDEC、实际音视频与键鼠表现。

## 参考

- 参考项目 `UURemote-web` 中的 `server/uu-api.mjs`、`public/lib/streamer.js`、`public/lib/protocol.js`、`public/lib/protobuf.js`。
- [libdatachannel](https://github.com/paullouisageneau/libdatachannel)：媒体接收与数据通道 API。
- [Switch SDL2 示例](https://github.com/switchbrew/switch-examples/tree/master/graphics/sdl2/sdl2-simple)：原生应用和依赖链接。
- [devkitPro FFmpeg 包构建](https://github.com/devkitPro/pacman-packages/blob/master/switch/ffmpeg/PKGBUILD)：Horizon/NVTEGRA 支持。
- [NVTEGRA FFmpeg 实现说明](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-May/328549.html)。
