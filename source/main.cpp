#include "client.hpp"
#include "qrcodegen.hpp"
#include <SDL_ttf.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sys/stat.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

// Visual thesis: quiet dark workspace, large readable type, a single mint focus.
// Content: account and connection status, computer list, QR/desktop workspace, controls.
// Interaction: eased focus movement, a restrained busy pulse, toggleable stream overlay.
namespace {
constexpr SDL_Color white{238, 243, 246, 255}, muted{142, 158, 169, 255}, accent{102, 231, 197, 255};
void color(SDL_Renderer *r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}
void rect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    color(r, c);
    SDL_Rect a{x, y, w, h};
    SDL_RenderFillRect(r, &a);
}
void text(SDL_Renderer *r, TTF_Font *f, const std::string &s, int x, int y, SDL_Color c = white,
          int maxWidth = 1200) {
    if (s.empty())
        return;
    auto *surface = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surface)
        return;
    auto *t = SDL_CreateTextureFromSurface(r, surface);
    SDL_Rect dst{x, y, surface->w, surface->h};
    if (dst.w > maxWidth) {
        SDL_Rect src{0, 0, maxWidth, surface->h};
        dst.w = maxWidth;
        SDL_RenderCopy(r, t, &src, &dst);
    } else
        SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(surface);
}
struct Fonts {
    TTF_Font *normal = nullptr, *small = nullptr, *large = nullptr;
    Fonts() {
#ifdef __SWITCH__
        PlFontData data{};
        if (R_FAILED(plGetSharedFontByType(&data, PlSharedFontType_ChineseSimplified)))
            throw std::runtime_error("Cannot load system font");
        auto load = [&](int size) {
            return TTF_OpenFontRW(SDL_RWFromConstMem(data.address, int(data.size)), 1, size);
        };
#else
        const char *env = std::getenv("UU_FONT");
        std::string path = env ? env : "C:/Windows/Fonts/msyh.ttc";
        auto load = [&](int size) { return TTF_OpenFont(path.c_str(), size); };
#endif
        normal = load(26);
        small = load(21);
        large = load(48);
        if (!normal || !small || !large)
            throw std::runtime_error(TTF_GetError());
    }
    ~Fonts() {
        TTF_CloseFont(normal);
        TTF_CloseFont(small);
        TTF_CloseFont(large);
    }
};
std::string read(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
struct Controls {
    bool confirm = false, back = false, refresh = false, logout = false, up = false, down = false,
         help = false, disconnect = false, exit = false;
    float mx = 0, my = 0, scroll = 0;
    uint64_t held = 0;
};
#ifdef __SWITCH__
struct FocusHook {
    AppletHookCookie cookie{};
    explicit FocusHook(uu::Client &client) {
        appletHook(
            &cookie,
            [](AppletHookType type, void *data) {
                if ((type == AppletHookType_OnFocusState &&
                     appletGetFocusState() != AppletFocusState_InFocus) ||
                    type == AppletHookType_OnExitRequest) {
                    static_cast<uu::Client *>(data)->command("release");
                }
            },
            &client);
    }
    ~FocusHook() {
        appletUnhook(&cookie);
    }
};
#endif
} // namespace

int main(int, char **) {
#ifdef __SWITCH__
    if (appletGetAppletType() != AppletType_Application &&
        appletGetAppletType() != AppletType_SystemApplication) {
        consoleInit(nullptr);
        printf("UU Remote NS requires full application mode.\nHold R while launching a game, then start this "
               "NRO.\nPress + to exit.\n");
        PadState p;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&p);
        while (appletMainLoop()) {
            padUpdate(&p);
            if (padGetButtonsDown(&p) & HidNpadButton_Plus)
                break;
            consoleUpdate(nullptr);
        }
        consoleExit(nullptr);
        return 0;
    }
    socketInitializeDefault();
    nifmInitialize(NifmServiceType_User);
    sslInitialize(0);
    romfsInit();
    plInitialize(PlServiceType_User);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    appletSetMediaPlaybackState(true);
#endif
    int result = 0;
    try {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
            throw std::runtime_error(SDL_GetError());
        if (TTF_Init() != 0)
            throw std::runtime_error(TTF_GetError());
        curl_global_init(CURL_GLOBAL_DEFAULT);
        {
            auto *window = SDL_CreateWindow("UU Remote NS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                            1280, 720, SDL_WINDOW_SHOWN);
            if (!window)
                throw std::runtime_error(SDL_GetError());
            auto *renderer =
                SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer)
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            if (!renderer)
                throw std::runtime_error(SDL_GetError());
            SDL_RenderSetLogicalSize(renderer, 1280, 720);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            Fonts fonts;
#ifdef __SWITCH__
            std::string key = read("romfs:/signing.key");
            const std::string profile = "sdmc:/switch/UURemote-ns/client-id";
            mkdir("sdmc:/switch/UURemote-ns", 0777);
#else
            std::string key = read("romfs/signing.key");
            const std::string profile = "client-id";
#endif
            if (key.size() != 24)
                throw std::runtime_error("Missing signing.key: run scripts/extract_key.py and rebuild");
            std::string id = read(profile);
            if (id.size() != 36) {
                id = uu::uuid();
                std::ofstream(profile) << id;
            }
            auto media = std::make_shared<uu::Media>();
            {
                uu::Client client(key, id, media);
#ifdef __SWITCH__
                FocusHook focusHook(client);
#endif
                bool running = true, overlay = true, wasStreaming = false, touchHeld = false;
                int selected = 0, texW = 0, texH = 0;
                uint64_t serial = 0, lastHeld = 0;
                float cursorX = .5f, cursorY = .5f, focusY = 230;
                Uint32 previous = SDL_GetTicks(), lastScroll = 0, lastPointer = 0;
                SDL_Texture *video = nullptr;
                std::string qrUrl;
                std::unique_ptr<qrcodegen::QrCode> qr;
                SDL_Rect viewport{0, 0, 1280, 720};
                auto send = [&](const uu::Json &data) { client.command("input", data); };
                while (running) {
#ifdef __SWITCH__
                    if (!appletMainLoop())
                        break;
#endif
                    Uint32 now = SDL_GetTicks();
                    float dt = std::min((now - previous) / 1000.f, .05f);
                    previous = now;
                    auto state = client.snapshot();
                    selected = std::clamp(selected, 0, std::max(0, int(state.devices.size()) - 1));
                    Controls c;
                    SDL_Event event;
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_QUIT)
                            running = false;
                        if (event.type == SDL_WINDOWEVENT &&
                            event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                            client.command("release");
                            touchHeld = false;
                        }
                        if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                            auto k = event.key.keysym.sym;
                            c.confirm |= k == SDLK_RETURN;
                            c.back |= k == SDLK_ESCAPE;
                            c.refresh |= k == SDLK_y;
                            c.logout |= k == SDLK_x;
                            c.up |= k == SDLK_UP;
                            c.down |= k == SDLK_DOWN;
                            c.help |= k == SDLK_F1;
                            c.disconnect |= k == SDLK_F2;
                            c.exit |= k == SDLK_F10;
                        }
#ifndef __SWITCH__
                        if (state.streaming &&
                            (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN ||
                             event.type == SDL_MOUSEBUTTONUP)) {
                            if (event.type == SDL_MOUSEMOTION) {
                                cursorX =
                                    std::clamp((event.motion.x - viewport.x) / float(viewport.w), 0.f, 1.f);
                                cursorY =
                                    std::clamp((event.motion.y - viewport.y) / float(viewport.h), 0.f, 1.f);
                                send({{"action", "mouse_move_absolute"},
                                      {"abs_x", cursorX},
                                      {"abs_y", cursorY}});
                            } else
                                send({{"action",
                                       event.type == SDL_MOUSEBUTTONDOWN ? "mouse_press" : "mouse_release"},
                                      {"button", event.button.button == SDL_BUTTON_LEFT ? 1 : 2}});
                        }
#endif
                    }
#ifdef __SWITCH__
                    padUpdate(&pad);
                    uint64_t pressed = padGetButtonsDown(&pad);
                    c.held = padGetButtons(&pad);
                    c.confirm |= pressed & HidNpadButton_A;
                    c.back |= pressed & HidNpadButton_B;
                    c.refresh |= pressed & HidNpadButton_Y;
                    c.logout |= pressed & HidNpadButton_X;
                    c.up |= pressed & HidNpadButton_Up;
                    c.down |= pressed & HidNpadButton_Down;
                    c.help |= pressed & HidNpadButton_Plus;
                    c.disconnect |= pressed & HidNpadButton_Minus;
                    c.exit |= !state.streaming && (pressed & HidNpadButton_Plus);
                    auto stick = padGetStickPos(&pad, 0), right = padGetStickPos(&pad, 1);
                    auto axis = [](int v) {
                        float n = v / 32767.f;
                        return std::abs(n) > .15f ? std::copysign((std::abs(n) - .15f) / .85f, n) : 0.f;
                    };
                    c.mx = axis(stick.x);
                    c.my = -axis(stick.y);
                    c.scroll = axis(right.y);
#endif
                    bool busy = state.stage != "idle" && state.stage != "error" && state.stage != "login";
                    if (c.exit)
                        running = false;
                    if (state.streaming) {
                        if (c.help)
                            overlay = !overlay;
                        if (c.disconnect) {
                            client.command("disconnect");
                            continue;
                        }
                        cursorX = std::clamp(cursorX + c.mx * dt * .7f, 0.f, 1.f);
                        cursorY = std::clamp(cursorY + c.my * dt * .7f, 0.f, 1.f);
                        if ((c.mx || c.my) && now - lastPointer >= 16) {
                            send({{"action", "mouse_move_absolute"}, {"abs_x", cursorX}, {"abs_y", cursorY}});
                            lastPointer = now;
                        }
                        if (c.scroll && now - lastScroll > 80) {
                            send({{"action", "mouse_scroll"},
                                  {"delta_x", 0},
                                  {"delta_y", int(c.scroll * 120)}});
                            lastScroll = now;
                        }
#ifdef __SWITCH__
                        for (auto [button, keycode] :
                             std::initializer_list<std::pair<uint64_t, int>>{{HidNpadButton_A, 13},
                                                                             {HidNpadButton_B, 27},
                                                                             {HidNpadButton_X, 32},
                                                                             {HidNpadButton_Y, 9},
                                                                             {HidNpadButton_L, 17},
                                                                             {HidNpadButton_R, 16},
                                                                             {HidNpadButton_Up, 87},
                                                                             {HidNpadButton_Down, 83},
                                                                             {HidNpadButton_Left, 65},
                                                                             {HidNpadButton_Right, 68}}) {
                            if ((c.held & button) != (lastHeld & button))
                                send({{"action", (c.held & button) ? "kbd_press" : "kbd_release"},
                                      {"key", keycode}});
                        }
                        for (auto [button, mouse] : std::initializer_list<std::pair<uint64_t, int>>{
                                 {HidNpadButton_ZR, 1}, {HidNpadButton_ZL, 2}})
                            if ((c.held & button) != (lastHeld & button))
                                send({{"action", (c.held & button) ? "mouse_press" : "mouse_release"},
                                      {"button", mouse}});
                        HidTouchScreenState touch{};
                        hidGetTouchScreenStates(&touch, 1);
                        bool inside = false;
                        if (touch.count) {
                            auto p = touch.touches[0];
                            inside = int(p.x) >= viewport.x && int(p.x) < viewport.x + viewport.w &&
                                     int(p.y) >= viewport.y && int(p.y) < viewport.y + viewport.h;
                            if (inside) {
                                cursorX = (p.x - viewport.x) / float(viewport.w);
                                cursorY = (p.y - viewport.y) / float(viewport.h);
                                send({{"action", "mouse_move_absolute"},
                                      {"abs_x", cursorX},
                                      {"abs_y", cursorY}});
                            }
                        }
                        if (inside != touchHeld) {
                            send({{"action", inside ? "mouse_press" : "mouse_release"}, {"button", 1}});
                            touchHeld = inside;
                        }
#else
                        if (c.confirm) {
                            send({{"action", "kbd_press"}, {"key", 13}});
                            send({{"action", "kbd_release"}, {"key", 13}});
                        }
                        if (c.back) {
                            send({{"action", "kbd_press"}, {"key", 27}});
                            send({{"action", "kbd_release"}, {"key", 27}});
                        }
#endif
                    } else {
                        if (wasStreaming) {
                            if (video) {
                                SDL_DestroyTexture(video);
                                video = nullptr;
                            }
                            serial = 0;
                            texW = texH = 0;
                            overlay = true;
                            touchHeld = false;
                        }
                        if (c.back || c.disconnect) {
                            client.command("cancel");
                        } else if (c.logout && state.authenticated)
                            client.command("logout");
                        else if (c.refresh && state.authenticated && !busy)
                            client.command("refresh");
                        else if (!busy && state.stage != "login") {
                            if (c.up)
                                selected = std::max(0, selected - 1);
                            if (c.down)
                                selected = std::min(std::max(0, int(state.devices.size()) - 1), selected + 1);
                            if (c.confirm) {
                                if (!state.authenticated)
                                    client.command("login");
                                else if (state.devices.empty())
                                    client.command("refresh");
                                else if (state.devices[selected].value("status", std::string{}) != "offline")
                                    client.command("connect",
                                                   {{"deviceId", state.devices[selected]["device_id"]}});
                            }
                        }
                    }
                    lastHeld = c.held;
                    wasStreaming = state.streaming;
                    color(renderer, {13, 19, 25, 255});
                    SDL_RenderClear(renderer);
                    if (state.streaming) {
                        auto p = media->picture(serial);
                        if (p.serial) {
                            if (p.width != texW || p.height != texH) {
                                if (video)
                                    SDL_DestroyTexture(video);
                                video = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                          SDL_TEXTUREACCESS_STREAMING, p.width, p.height);
                                texW = p.width;
                                texH = p.height;
                            }
                            if (video)
                                SDL_UpdateTexture(video, nullptr, p.pixels.data(), p.width * 4);
                            serial = p.serial;
                            float scale = std::min(1280.f / p.width, 720.f / p.height);
                            viewport = {int((1280 - p.width * scale) / 2), int((720 - p.height * scale) / 2),
                                        int(p.width * scale), int(p.height * scale)};
                        }
                        if (video)
                            SDL_RenderCopy(renderer, video, nullptr, &viewport);
                        else
                            text(renderer, fonts.normal, "等待电脑发送画面…", 470, 330, muted);
                        int x = viewport.x + int(cursorX * viewport.w),
                            y = viewport.y + int(cursorY * viewport.h);
                        rect(renderer, x - 5, y, 11, 2, accent);
                        rect(renderer, x, y - 5, 2, 11, accent);
                        if (overlay) {
                            rect(renderer, 0, 0, 1280, 68, {13, 19, 25, 225});
                            text(renderer, fonts.normal, "UU远程", 28, 18);
                            text(renderer, fonts.small,
                                 "左摇杆 鼠标  ·  ZR/ZL 点击  ·  A 确认  ·  B Esc  ·  − 断开  ·  + 隐藏", 200,
                                 24, muted);
                            rect(renderer, 0, 650, 1280, 70, {13, 19, 25, 225});
                            text(renderer, fonts.small,
                                 "方向键 WASD  ·  X 空格  ·  Y Tab  ·  L Ctrl  ·  R Shift  ·  右摇杆 滚轮",
                                 28, 665, muted);
                            text(renderer, fonts.small, media->status(), 28, 694, muted);
                        }
                    } else {
                        text(renderer, fonts.large, "UU远程", 62, 44);
                        text(renderer, fonts.small, "NINTENDO SWITCH", 270, 69, accent);
                        text(renderer, fonts.small, state.authenticated ? state.nickname : "尚未登录", 930,
                             69, muted, 280);
                        rect(renderer, 62, 127, 1156, 1, {44, 56, 67, 255});
                        text(renderer, fonts.small, "我的电脑", 62, 167, muted);
                        if (!state.devices.empty()) {
                            selected = std::clamp(selected, 0, int(state.devices.size()) - 1);
                            int first = std::max(0, selected - 3);
                            float target = 223.f + (selected - first) * 88;
                            focusY += (target - focusY) * std::min(1.f, dt * 18);
                            rect(renderer, 62, int(focusY), 535, 78, {32, 50, 53, 255});
                            rect(renderer, 62, int(focusY), 4, 78, accent);
                            for (int i = first; i < std::min(first + 4, int(state.devices.size())); ++i) {
                                auto &d = state.devices[i];
                                int y = 232 + (i - first) * 88;
                                bool online = d.value("status", std::string{}) != "offline";
                                text(renderer, fonts.normal, d.value("alias", std::string("电脑")), 84, y,
                                     white, 470);
                                text(renderer, fonts.small, online ? "在线" : "离线", 84, y + 35,
                                     online ? accent : muted);
                            }
                        } else {
                            text(renderer, fonts.normal,
                                 state.authenticated ? "暂未发现电脑" : "登录你的 UU 账号", 62, 237);
                            text(renderer, fonts.small,
                                 state.authenticated ? "请在电脑上登录同一账号"
                                                     : "手机扫码，即可同步远程电脑",
                                 62, 287, muted);
                            rect(renderer, 62, 350, 330, 64, accent);
                            text(renderer, fonts.normal, state.authenticated ? "Y  刷新设备" : "A  扫码登录",
                                 88, 367, {13, 19, 25, 255});
                        }
                        if (state.qrUrl != qrUrl) {
                            qrUrl = state.qrUrl;
                            qr.reset();
                            if (!qrUrl.empty())
                                qr = std::make_unique<qrcodegen::QrCode>(qrcodegen::QrCode::encodeText(
                                    qrUrl.c_str(), qrcodegen::QrCode::Ecc::MEDIUM));
                        }
                        if (qr) {
                            int scale = std::max(1, 280 / (qr->getSize() + 8)),
                                size = (qr->getSize() + 8) * scale;
                            int x = 893 - size / 2, y = 220;
                            rect(renderer, x, y, size, size, {255, 255, 255, 255});
                            for (int j = 0; j < qr->getSize(); ++j)
                                for (int i = 0; i < qr->getSize(); ++i)
                                    if (qr->getModule(i, j))
                                        rect(renderer, x + (i + 4) * scale, y + (j + 4) * scale, scale, scale,
                                             {0, 0, 0, 255});
                            text(renderer, fonts.small, "使用手机 UU远程 App 扫码", 730, 535, muted);
                        } else {
                            rect(renderer, 718, 237, 356, 214, {28, 40, 49, 255});
                            rect(renderer, 729, 248, 334, 192, {17, 25, 33, 255});
                            rect(renderer, 886, 451, 20, 45, {44, 56, 67, 255});
                            rect(renderer, 837, 495, 118, 6, {44, 56, 67, 255});
                            text(renderer, fonts.small, "独立连接原版 UU 电脑端", 762, 535, muted);
                        }
                        if (busy || state.stage == "login") {
                            float pulse = (std::sin(now / 250.f) + 1) / 2;
                            rect(renderer, 62, 591, 6, 26,
                                 {accent.r, accent.g, accent.b, uint8_t(100 + pulse * 155)});
                        }
                        text(renderer, fonts.small, state.detail, 82, 594,
                             state.stage == "error" ? SDL_Color{255, 157, 141, 255} : muted, 1110);
                        rect(renderer, 62, 653, 1156, 1, {44, 56, 67, 255});
                        text(renderer, fonts.small, "A 确认    B 返回    Y 刷新    X 退出账号", 62, 675,
                             muted);
                        text(renderer, fonts.small, "+ 退出", 1120, 675, muted);
                    }
                    SDL_RenderPresent(renderer);
                }
                if (video)
                    SDL_DestroyTexture(video);
            }
            rtc::Cleanup().wait();
            media.reset();
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
        }
        curl_global_cleanup();
        TTF_Quit();
        SDL_Quit();
    } catch (const std::exception &e) {
        result = 1;
#ifdef __SWITCH__
        SDL_Quit();
        consoleInit(nullptr);
        printf("UU Remote NS\n%s\nPress + to exit.\n", e.what());
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
                break;
            consoleUpdate(nullptr);
        }
        consoleExit(nullptr);
#else
        std::cerr << e.what() << '\n';
#endif
    }
#ifdef __SWITCH__
    appletSetMediaPlaybackState(false);
    plExit();
    romfsExit();
    sslExit();
    nifmExit();
    socketExit();
#endif
    return result;
}
