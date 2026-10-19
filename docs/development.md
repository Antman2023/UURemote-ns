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

`source/auth.cpp` 校验登录响应和短信参数，复用参考 Web 端的验证码发送与登录 API，成功发送后限制 60 秒内重发。`source/diagnostics.cpp` 将固定连接阶段写入 SD 卡日志，异常时附加类型、数值错误码和最多 16 帧代码偏移；文件超过 128 KiB 后重建，不记录异常消息原文或协议数据。

`source/background.cpp` 为候选地址解析和 RTC 清理保留异步任务的共享 future；完成后回收线程，正常或异常退出时均在关闭网络服务前等待剩余任务。Switch 不使用 `std::thread::detach`。

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
- 本机原生 WebRTC 回环通过：连续三轮初始化与清理，覆盖 ICE、DTLS、五个 SCTP 数据通道、二进制键盘消息、SRTP H.264 与 Opus RTP 重组。
- 短信 API 参数、冷却时间、失败重试、前导零验证码和登录响应校验通过模拟请求测试；未发送真实短信。
- 使用实际 qrcodegen 编码器与新版布局生成 477×477 像素的合成登录二维码，经图像解码恢复出完整原始 URL；手机实扫仍需真机复测。
- Horizon 使用的 pthread 随机数分支通过 8 线程并发初始化、状态保留、线程隔离和退出清理测试。
- Horizon 的 usrsctp 内部 C 类型与 libdatachannel 外部 C++ 类型均通过地址长度、地址族偏移与字段宽度的编译期断言；ARM64 实际初始化指令中的地址前缀为 `10 7b`（长度 16、地址族 123）。
- 在 GNU 本机回环中将 `pthread_detach` 强制替换为返回 ENOSYS，先验证 `std::thread::detach` 确实抛错，再验证域名候选解析、ICE / DTLS / SCTP / SRTP 和清理全程不调用 detach。后台任务并发、嵌套、移动参数、异常保留及退出等待测试通过。
- NRO0 与 ASET 资源段检查、ZIP CRC 和包内 NRO 一致性检查通过。
- 已收到用户真机连接阶段崩溃报告并完成符号定位；修复后的正式房间互通、短信实收、NVDEC、实际音视频与键鼠表现仍需真机复测。

## v0.1.0 连接崩溃定位

恢复发布版 ELF 后，其 Build ID `86ad8d2606b95ba1b9a6bbe9844a7acbde840f9a` 与用户报告完全一致。`2168-0002` 的 PC 偏移 `0xdc22c` 对应 `rtc::impl::IceTransport::IceTransport`，上层依次为 `initIceTransport`、`setLocalDescription` 和 `Client::beginPeer`。

故障指令位于 ICE 服务器随机排序前，访问模板函数中的 `thread_local` 随机数状态；该构建生成了错误的 `[thread_pointer + thread_pointer]` 地址。v0.1.1 在 Horizon 上使用 pthread key 保存每线程引擎，在线程退出时释放；其他平台保留原实现。已确认修复后的 ICE 目标文件调用 `pthread_getspecific` / `pthread_setspecific`，不再含原先的 `__aarch64_read_tp` 访问。补丁由 `scripts/patch_deps.py` 重复应用，不改动上游锁定版本。

每版排查应保存对应的 `build-nx/UURemote-ns.elf` 与 `.map`，并核对 Build ID。原始用户崩溃报告不纳入仓库或发布包。

## v0.1.1 连接后异常终止排查

用户截图显示 `2345-0008 (0x1159)`，但 PC 和回溯均为零。连接日志表明已执行 `rtc-offer`，约 5 秒后进入 `runtime-terminate`；因此该截图不足以定位最初异常。

检查发布版二进制发现 `pthread_detach` 固定返回 88（ENOSYS），而上游在解析非数值 ICE 候选地址和清理 RTC 运行时时调用 `std::thread::detach`。分离失败后，仍可 join 的线程在析构时触发 terminate。v0.1.2 用保留共享 future 的异步任务替换这两处，确认最终 Switch ELF 不再含 `pthread_detach` / `std::thread::detach`。

该兼容缺陷已通过禁用 detach 的本机回环覆盖；是否就是用户此次异常的直接触发点，仍需真机复测。终止处理器现在记录异常类型、数值错误码和代码偏移，再直接退出进程，避免 libc 的全局清理与仍在呈现画面的 UI 线程发生竞争。

## v0.1.2 SCTP 堆越界定位

用户报告的 Build ID `5449a2e3c1b942cde71efc81eb5317fd3633f6ea` 与保存的 v0.1.2 ELF 一致。`2168-0002` 的 PC `0x10b7858` 位于 `_malloc_r`；调用栈已通过 DTLS 握手，正在 `SctpTransport` 构造函数末尾插入实例。崩溃前刚调用了 `usrsctp_register_address`。

Horizon 的 `sockaddr` 首两字节是长度和地址族；usrsctp 公共头按已知 BSD 系统宏选择这种布局，但没有识别 `__SWITCH__`，导致 `sockaddr_conn` 使用两个字节的地址族，并将 `HAVE_SCONN_LEN` 缓存为 false。内部却正确启用了 `HAVE_SA_LEN`。因此 `sctp_add_addr_to_vrf` 将 `AF_CONN=123` 读作 `sa_len`，把栈上的 123 字节复制进 32 字节的地址联合体，覆盖后续字段及堆元数据。该复制发生后，下一个 16 字节分配在 malloc 内崩溃。

v0.1.3 将 Horizon 加入长度前缀分支，重新检测已缓存的 `have_sconn_len`，并将 `HAVE_SCONN_LEN` 同时传给 C 库和 C++ 适配层。两处都包含 `switch_compat/sctp_layout.h`，检查长度、地址族的偏移、字段宽度和目标容量。`python tests/switch_sctp_layout_test.py` 用实际 SDK 头文件分别交叉编译 C / C++，验证正确布局通过、旧布局被拒绝。旧版 `usrsctp_register_address` 的实际 ARM64 初始化值为 `0x007b`；新版该函数及 C++ `getSockAddrConn` 均为 `0x7b10`，小端字节序下即长度 16、地址族 123。

本机回环使用宿主 socket 布局，不能覆盖 Horizon ABI；本次额外核对了目标编译和二进制指令。修复后的正式串流仍需真机复测，原始用户报告继续保留在仓库外。

## v0.1.3 协商失败与 v0.1.4 诊断

用户日志中，v0.1.3 在 `rtc-offer` 后约 41 秒记录 `client-exception`、`disconnect`、`error`，未出现 `runtime-terminate`。日志缺少远端描述、候选和各层状态，不能确认这是 ICE 失败、DTLS 失败、SCTP 超时还是其他异常。

与参考 Web 实现对照后，补齐 `include/rtc_signaling.hpp` 中的候选暂存：远端 SDP 尚未设置时保留候选，设置描述并准备好本地回答后按顺序应用，最多 128 条；断开时清空。本机回环使用同一处理函数，故意在 Offer 和 Answer 前发送候选，覆盖扁平 / 嵌套格式以及完整音视频、五个数据通道与连续三轮清理。该顺序缺陷已修复，但旧日志不足以证明它触发了本次真机失败。

`source/rtc_diagnostics.cpp` 将允许的库日志前缀映射成固定事件，只提取有限位数的数值错误码，其余文本全部丢弃。补丁仅检查 SCTP 包第一个 chunk 的类型，记录 INIT、INIT ACK、COOKIE ECHO、COOKIE ACK 和 ABORT 的收发方向，不保存包内容。Mbed TLS 异常增加数值错误码以便诊断；日志脱敏测试覆盖敏感尾部和未知消息被丢弃。

真机排查时先确认 `app-start-0.1.4`，再看 `soac-offer-sent`、`soac-answer-received` 与 `soac-description-applied`；之后依次检查 ICE、DTLS、SCTP。ICE 状态值来自锁定版 libdatachannel：0 New、1 Checking、2 Connected、3 Completed、4 Failed、5 Disconnected、6 Closed。`rtc-dtls-connected` 表示完成 DTLS 握手，`rtc-sctp-connected` 表示数据通道传输已建立；缺少某个事件本身不能证明上一层失败，需结合状态、错误和超时判断。

## v0.1.5 关闭后候选处理

截图显示 `Got a remote candidate without ICE transport`。在锁定版 libdatachannel 中，`processRemoteCandidate` 只有在远端 SDP 存在而 ICE transport 指针为空时抛出该错误。`closeTransports` 先进入 Closed 状态，再清空传输指针，但保留 SDP；因此该消息可能来自最初失败后的迟到候选，不能直接当作失败起因。客户端先批量处理回调事件，再读信令；批量读取过程中发生关闭时，新候选可能早于下一轮状态事件处理而抛错。

远端信令处理现在检查 Failed / Disconnected / Closed 及 ICE Closed，清空待用候选并返回关闭标记。处理期间若遇到 logic_error，仅在连接已关闭时忽略，活动连接的异常继续传播。原先排队的连接状态事件仍由客户端正常处理，不会因这个保护而自动重连或掩盖持续失败。

本机回环在完成五通道音视频并关闭后，确认远端 SDP 仍存在，随后注入候选和无效的迟到 SDP，验证不再调用已关闭的传输或保留旧候选。用户随后复制的最新日志仍以 `app-start-0.1.3` 启动，说明尚未运行诊断版；v0.1.5 首页增加来自同一 CMake 版本定义的版本号，避免仅靠文件名判断更新是否生效。

## v0.1.6 协商期间事件处理延迟

v0.1.5 真机日志已记录 Offer 发出、Answer 收到并应用、远端候选持续应用，但从首次状态之后再无主线程记录的 RTC 状态，54 秒后进入连接超时；DTLS `-29312`（`MBEDTLS_ERR_SSL_CONN_EOF`）出现在 disconnect 之后，符合退出时停止接收队列的情况，不能单独据此判定 DTLS 原始失败。

旧 `Client::run` 在同一循环里读取最多 64 条信令，甚至在检查条数上限前又调用一次 receive，之后先检查超时，到下一轮才处理回调。只要读取持续耗时，连接与收集状态等事件就会滞留。`pumpSignaling` 现在以 16 条 / 8 ms 为预算，并在出现待处理事件、取消或退出时让出；随后调用 `drainEvents`，再检查心跳与连接期限。慢速读取超过 100 ms 和超时时的实际状态只记录数值，不含协议内容。

Switch 最终链接通过 `--wrap=sslConnectionRead` / `--wrap=sslConnectionWrite`，在数据读写时显式使用 `SslIoMode_NonBlocking`；握手本身、HTTPS 证书和主机名验证沿用现有实现。该适配使用公开 libnx API，避免依赖 curl 私有上下文布局。实际 SDK 的 libcurl 目标文件已经含有设置 I/O 模式的调用，不能把“库从未设置非阻塞”当成已确认原因；这里的包装是在数据入口保证 CONNECT_ONLY 所需的非阻塞契约。devkitPro 后端实现可参考[官方补丁](https://github.com/devkitPro/pacman-packages/blob/master/switch/curl/switch-curl.patch)，本地锁定 SDK 二进制也单独核对。

测试覆盖持续来包、单次读取耗尽时间预算、取消时不再读取；TLS 模拟服务验证读写在非阻塞模式下执行，WouldBlock、部分读写和设置模式失败均完整保留。交叉编译使用真实 libnx 头；最终 ELF 的 libnx_recv / libnx_send 分别调用包装函数，包装函数调用真实 SSL 服务入口。真机是否能完成 DTLS / SCTP 仍需更新后验证。

## 未发布修复：事件恢复与 I/O 期限

代码检查发现，客户端原先把整批事件移到局部队列后处理；任一处理器抛错时，剩余事件随栈展开一起销毁，可能丢失已经排队的退出账号或取消命令。现在按批次初始数量逐条取出，处理器在锁外运行；异常时保留剩余事件，处理器新加入的事件留到下一批，连接代次过滤保持有效。新增回归覆盖处理失败、处理器追加事件和中途停止。

信令发送与 WebSocket 升级握手原来只在 `CURLE_AGAIN` 时检查超时。持续成功返回少量字节可能绕过期限；现在每次重试前统一检查单调时钟期限，即使仍有少量进展，也受原定 5 秒 / 15 秒限制。

本机 7 项测试及 Switch 交叉编译通过。模拟器已安装到项目工具目录，但当前仍缺少 `prod.keys` 和固件，停在 RYU-0001 提示；尚未完成 NRO 启动和正式 UU 连接测试。这些未发布改动不能视为已经解决真机协商故障。

## 未发布修复：媒体生命周期与完整 Windows 调试

媒体构造函数打开 SDL 音频设备后，如果 Opus 初始化或线程创建失败，旧实现没有回收已创建的资源。现在在构造函数体内捕获失败并清理。视频线程增加异常边界，FFmpeg 上下文、帧、包及缩放器统一用作用域对象释放，避免像素分配或缩放异常直接终止进程；失败后停止接受新视频包，并提示重新启动程序。

`Media::reset` 现在持有视频队列锁后才推进连接代次，并在同一临界区清空旧帧。旧顺序在加锁前推进代次，可能让解码线程把仍在队列中的旧帧标记为新连接帧。连接超时、心跳、二维码期限和短信冷却统一使用 `monotonicMs`；API 签名和连接协议中的 Unix 时间戳继续使用 `nowMs`。

新增 `UU_NATIVE_TESTS` 和 `scripts/build-native.ps1`，以真实 FFmpeg / SDL / Opus 运行 16×16 合成红色视频，验证 RGBA 输出、重置后的旧代次丢弃、新帧恢复，并在 GNU 构建中注入帧分配失败、缩放器异常和 Opus 初始化失败，检查资源释放及后续音频设备可重新打开。本机 7 项核心测试、真实媒体集成测试和 Switch ARM64 构建通过。

完整 Windows 客户端的首次运行暴露了两项环境问题：GCC 15 产物与 GCC 16 运行库的 TLS 符号不匹配；便携 curl 指向未生成的 CA 文件。原生 SDK 现锁定同一套编译器及依赖，下载 SHA256 记录在 `scripts/sdk-native-windows.lock.json`；Windows 请求使用原生证书库，保留证书链与主机名验证，参考 [curl 的原生 CA 选项](https://curl.se/libcurl/c/CURLOPT_SSL_OPTIONS.html)。

2026-10-19，用户扫码确认后，本机完整客户端实际完成设备同步、ICE / DTLS / SCTP 协商，并显示原版电脑端的远程锁屏画面；随后两轮断开重连均成功。共三次真实连接，只有首次通过截图确认画面，不将此扩展为每轮音视频和输入均已验证。没有将用户二维码、桌面截图、账号凭据或原始日志纳入仓库。

尚待验证：Switch 真机网络与 NVDEC、实际音频播放、完整输入语义和长期稳定性。本机短信键盘仍是空实现；模拟器仍缺少密钥与固件。Windows 调试环境用于缩短协议排查周期，Switch 运行时继续独立连接 UU。

## 参考

- 参考项目 `UURemote-web` 中的 `server/uu-api.mjs`、`public/lib/streamer.js`、`public/lib/protocol.js`、`public/lib/protobuf.js`。
- [libdatachannel](https://github.com/paullouisageneau/libdatachannel)：媒体接收与数据通道 API。
- [Switch SDL2 示例](https://github.com/switchbrew/switch-examples/tree/master/graphics/sdl2/sdl2-simple)：原生应用和依赖链接。
- [devkitPro FFmpeg 包构建](https://github.com/devkitPro/pacman-packages/blob/master/switch/ffmpeg/PKGBUILD)：Horizon/NVTEGRA 支持。
- [NVTEGRA FFmpeg 实现说明](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-May/328549.html)。
