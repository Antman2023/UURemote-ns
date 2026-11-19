#include "background.hpp"
#include "client.hpp"
#include "controller_input.hpp"
#include "diagnostics.hpp"
#include "qrcodegen.hpp"
#include "remote_keyboard.hpp"
#include "rtc_diagnostics.hpp"
#include <SDL_ttf.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <sys/stat.h>
#include <tuple>
#include <utility>
#include <vector>
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
struct CachedLabel {
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture{nullptr, SDL_DestroyTexture};
    int width = 0, height = 0;

    CachedLabel(SDL_Renderer *renderer, TTF_Font *font, const char *label, SDL_Color tint) {
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(TTF_RenderUTF8_Blended(font, label, tint),
                                                                      SDL_FreeSurface);
        if (!surface)
            return;
        texture.reset(SDL_CreateTextureFromSurface(renderer, surface.get()));
        width = surface->w;
        height = surface->h;
    }

    void draw(SDL_Renderer *renderer, int x, int y) const {
        if (texture) {
            const SDL_Rect position{x, y, width, height};
            SDL_RenderCopy(renderer, texture.get(), nullptr, &position);
        }
    }
};
enum class ButtonIcon {
    A, B, X, Y, L, R, ZL, ZR, Plus, Minus, Dpad, UpDown, LeftStick, RightStick, RightClick,
    Enter, Esc, F1, F2, F3, F10, Count
};
struct ButtonHint {
    ButtonIcon icon;
    const char *label;
    ButtonIcon second = ButtonIcon::Count;
};
class ButtonPrompts {
    static constexpr int cellWidth = 64, cellHeight = 52, displayWidth = 40;
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> atlas_{nullptr, SDL_DestroyTexture};
    std::map<std::tuple<TTF_Font *, std::string_view, Uint32>, CachedLabel> captions_;

  public:
    explicit ButtonPrompts(SDL_Renderer *renderer) {
#ifdef __SWITCH__
        const char *path = "romfs:/ui/button-prompts.bmp";
#else
        const char *path = "romfs/ui/button-prompts.bmp";
#endif
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(SDL_LoadBMP(path), SDL_FreeSurface);
        if (!surface || surface->w != cellWidth * int(ButtonIcon::Count) || surface->h != cellHeight)
            throw std::runtime_error("Unable to load the button prompt images");
        SDL_SetColorKey(surface.get(), SDL_TRUE, SDL_MapRGB(surface->format, 0, 0, 0));
        atlas_.reset(SDL_CreateTextureFromSurface(renderer, surface.get()));
        if (!atlas_)
            throw std::runtime_error(SDL_GetError());
    }

    void draw(SDL_Renderer *renderer, TTF_Font *font, int x, int y, std::initializer_list<ButtonHint> hints,
              SDL_Color tint = muted) {
        const auto image = [&](ButtonIcon icon) {
            const SDL_Rect source{cellWidth * int(icon), 0, cellWidth, cellHeight}, target{x, y, displayWidth, 32};
            SDL_RenderCopy(renderer, atlas_.get(), &source, &target);
            x += displayWidth;
        };
        for (const auto &hint : hints) {
            image(hint.icon);
            if (hint.second != ButtonIcon::Count)
                image(hint.second);
            const auto rgba = (Uint32(tint.r) << 24) | (Uint32(tint.g) << 16) | (Uint32(tint.b) << 8) | tint.a;
            const auto key = std::make_tuple(font, std::string_view(hint.label), rgba);
            const auto &caption = captions_.try_emplace(key, renderer, font, hint.label, tint).first->second;
            caption.draw(renderer, x + 3, y + (32 - caption.height) / 2);
            x += caption.width + 21;
        }
    }
};
#ifdef __SWITCH__
constexpr auto confirmIcon = ButtonIcon::A, backIcon = ButtonIcon::B, exitIcon = ButtonIcon::Plus;
#else
constexpr auto confirmIcon = ButtonIcon::Enter, backIcon = ButtonIcon::Esc, exitIcon = ButtonIcon::F10;
#endif

class KeyboardLabels {
    std::vector<CachedLabel> labels_;

  public:
    void draw(SDL_Renderer *renderer, TTF_Font *font, const uu::RemoteKeyboard &keyboard) {
        using Layout = uu::RemoteKeyboard;
        if (labels_.empty()) {
            for (const auto &row : Layout::rows)
                for (int column = 0; column < row.count; ++column)
                    labels_.emplace_back(renderer, font, row.keys[column].label, white);
        }
        std::size_t index = 0;
        for (int row = 0; row < int(std::size(Layout::rows)); ++row)
            for (int column = 0; column < Layout::rows[row].count; ++column) {
                const auto box = Layout::bounds(row, column);
                const bool active = keyboard.armed(Layout::rows[row].keys[column].code);
                rect(renderer, box.x, box.y, box.width, box.height,
                     active ? SDL_Color{43, 86, 74, 255} : SDL_Color{31, 45, 55, 255});
                if (keyboard.selected(row, column)) {
                    color(renderer, accent);
                    const SDL_Rect outline{box.x, box.y, box.width, box.height};
                    SDL_RenderDrawRect(renderer, &outline);
                }
                const auto &label = labels_[index++];
                label.draw(renderer, box.x + (box.width - label.width) / 2, box.y + (box.height - label.height) / 2);
            }
    }
};
void statusText(SDL_Renderer *renderer, TTF_Font *font, const std::string &message, SDL_Color tint) {
    auto *surface = TTF_RenderUTF8_Blended_Wrapped(font, message.substr(0, 512).c_str(), tint, 1110);
    if (!surface)
        return;
    auto *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect source{0, 0, surface->w, std::min(surface->h, 64)}, destination{82, 580, source.w, source.h};
    if (texture)
        SDL_RenderCopy(renderer, texture, &source, &destination);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}
std::optional<std::string> numericInput(const char *title, const std::string &value, int minimum,
                                        int maximum) {
#ifdef __SWITCH__
    SwkbdConfig keyboard{};
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
        return std::nullopt;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetType(&keyboard, SwkbdType_NumPad);
    swkbdConfigSetHeaderText(&keyboard, title);
    swkbdConfigSetInitialText(&keyboard, value.c_str());
    swkbdConfigSetStringLenMin(&keyboard, minimum);
    swkbdConfigSetStringLenMax(&keyboard, maximum);
    swkbdConfigSetOkButtonText(&keyboard, "确定");
    std::array<char, 64> buffer{};
    auto result = swkbdShow(&keyboard, buffer.data(), buffer.size());
    swkbdClose(&keyboard);
    if (R_FAILED(result))
        return std::nullopt;
    return std::string(buffer.data());
#else
    (void)title;
    (void)value;
    (void)minimum;
    (void)maximum;
    return std::nullopt;
#endif
}
struct SmsForm {
    std::string country = "86", mobile, code;
    int selected = 1;
    uu::Json credentials() const {
        return {{"countryCode", country}, {"mobile", mobile}, {"code", code}};
    }
};
struct Fonts {
    TTF_Font *normal = nullptr, *small = nullptr, *large = nullptr;
    Fonts() {
#ifdef __SWITCH__
        PlFontData data{};
        const bool hasSystemFont = R_SUCCEEDED(plGetSharedFontByType(&data, PlSharedFontType_ChineseSimplified));
        auto load = [&](int size) {
            if (hasSystemFont) {
                if (auto *font = TTF_OpenFontRW(SDL_RWFromConstMem(data.address, int(data.size)), 1, size)) {
                    constexpr std::u32string_view required = U"远程登录扫码验证码设备连接退出确认鼠标控制网络键盘退格空格↑↓←→";
                    if (std::all_of(required.begin(), required.end(), [font](char32_t ch) {
                            return TTF_GlyphIsProvided32(font, ch) != 0;
                        }))
                        return font;
                    TTF_CloseFont(font);
                }
            }
            return TTF_OpenFont("romfs:/fonts/uu-cjk-fallback.otf", size);
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
         left = false, right = false, help = false, keyboard = false, disconnect = false, exit = false;
    float mx = 0, my = 0, scroll = 0;
    uint64_t held = 0;
    std::optional<std::pair<int, int>> tap;
};
#ifdef __SWITCH__
constexpr std::pair<uint64_t, int> controllerKeys[] = {
    {HidNpadButton_A, 13}, {HidNpadButton_B, 27}, {HidNpadButton_X, 32}, {HidNpadButton_Y, 9},
    {HidNpadButton_L, 17}, {HidNpadButton_R, 16}, {HidNpadButton_Up, 87}, {HidNpadButton_Down, 83},
    {HidNpadButton_Left, 65}, {HidNpadButton_Right, 68},
};

uu::ControllerSample sampleController(uu::ControllerInput &input, uint64_t buttons, uu::InputArea area) {
    HidTouchScreenState touch{};
    std::array<uu::TouchContact, std::size(touch.touches)> contacts;
    std::size_t count = 0;
    if (hidGetTouchScreenStates(&touch, 1)) {
        for (int i = 0; i < std::min(touch.count, int(contacts.size())); ++i) {
            const auto &contact = touch.touches[i];
            if (!(contact.attributes & HidTouchAttribute_End))
                contacts[count++] = {contact.finger_id, int(contact.x), int(contact.y)};
        }
    }
    return input.sample(buttons, HidNpadButton_ZR, contacts.data(), count, area);
}

struct FocusHook {
    AppletHookCookie cookie{};
    uu::Client &client;
    std::atomic<bool> resetInput{false};
    explicit FocusHook(uu::Client &client) : client(client) {
        appletHook(
            &cookie,
            [](AppletHookType type, void *data) {
                if ((type == AppletHookType_OnFocusState &&
                     appletGetFocusState() != AppletFocusState_InFocus) ||
                    type == AppletHookType_OnExitRequest) {
                    auto &hook = *static_cast<FocusHook *>(data);
                    hook.client.command("release");
                    hook.resetInput = true;
                }
            },
            this);
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
            const bool softwareVideo = std::ifstream("sdmc:/switch/UURemote-ns/software-video").is_open();
#else
            std::string key = read("romfs/signing.key");
            const std::string profile = "client-id";
            const bool softwareVideo = true;
#endif
            if (key.size() != 24)
                throw std::runtime_error("Missing signing.key: run scripts/extract_key.py and rebuild");
            uu::diagnostic("app-start-" UU_VERSION);
            std::set_terminate(uu::terminateWithDiagnostics);
            rtc::InitLogger(rtc::LogLevel::Info, [](rtc::LogLevel, const std::string &message) {
                const auto event = uu::rtcLogEvent(message);
                if (!event.empty())
                    uu::diagnostic(event.c_str());
            });
            std::string id = read(profile);
            if (id.size() != 36) {
                id = uu::uuid();
                std::ofstream(profile) << id;
            }
            uu::diagnostic(softwareVideo ? "video-decoder-software" : "video-decoder-automatic");
            auto media = std::make_shared<uu::Media>(softwareVideo ? uu::Media::VideoDecoder::Software
                                                                  : uu::Media::VideoDecoder::Automatic);
            {
                uu::Client client(key, id, media);
                ButtonPrompts prompts(renderer);
                KeyboardLabels keyboardLabels;
                uu::RemoteKeyboard keyboard;
#ifdef __SWITCH__
                FocusHook focusHook(client);
                uu::ControllerInput controllerInput;
                Uint32 lastScroll = 0, lastPointer = 0;
#else
                bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
                Uint32 heldMouse = 0;
#endif
                bool running = true, overlay = true, wasStreaming = false, keyboardOpen = false;
                int selected = 0, texW = 0, texH = 0;
                uint64_t serial = 0;
                float cursorX = .5f, cursorY = .5f, focusY = 230;
                Uint32 previous = SDL_GetTicks();
                using VideoTexture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
                VideoTexture video(nullptr, SDL_DestroyTexture);
                std::string qrUrl;
                std::unique_ptr<qrcodegen::QrCode> qr;
                SmsForm sms;
                SDL_Rect viewport{0, 0, 1280, 720};
                while (running) {
#ifdef __SWITCH__
                    if (!appletMainLoop())
                        break;
                    const bool focused = appletGetFocusState() == AppletFocusState_InFocus;
#endif
                    const Uint32 now = SDL_GetTicks();
                    const float dt = std::min((now - previous) / 1000.f, .05f);
                    previous = now;
                    auto state = client.snapshot();
                    const auto send = [&client, generation = state.inputGeneration](uu::Json data) {
                        client.sendInput(generation, std::move(data));
                    };
                    const auto movePointer = [&](uu::PointerPosition position) {
                        cursorX = position.x;
                        cursorY = position.y;
                        send({{"action", "mouse_move_absolute"}, {"abs_x", cursorX}, {"abs_y", cursorY}});
                    };
#ifdef __SWITCH__
                    if (focusHook.resetInput.exchange(false) || !focused || !state.streaming)
                        controllerInput.reset();
#else
                    if (!state.streaming)
                        heldMouse = 0;
#endif
                    const uu::InputArea inputArea =
                        video ? uu::InputArea{viewport.x, viewport.y, viewport.w, viewport.h} : uu::InputArea{};
                    selected = std::clamp(selected, 0, std::max(0, int(state.devices.size()) - 1));
                    Controls c;
                    SDL_Event event;
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_QUIT)
                            running = false;
#ifndef __SWITCH__
                        if (event.type == SDL_WINDOWEVENT) {
                            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                                focused = false;
                                client.command("release");
                                heldMouse = 0;
                                c = {};
                            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                                focused = true;
                        }
#endif
                        if (!focused || !running)
                            continue;
                        if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                            auto k = event.key.keysym.sym;
                            c.confirm |= k == SDLK_RETURN;
                            c.back |= k == SDLK_ESCAPE;
                            c.refresh |= k == SDLK_y;
                            c.logout |= k == SDLK_x;
                            c.up |= k == SDLK_UP;
                            c.down |= k == SDLK_DOWN;
                            c.left |= k == SDLK_LEFT;
                            c.right |= k == SDLK_RIGHT;
                            c.help |= k == SDLK_F1;
                            c.disconnect |= k == SDLK_F2;
                            c.keyboard |= k == SDLK_F3;
                            c.exit |= k == SDLK_F10;
                        }
#ifndef __SWITCH__
                        if (state.streaming && (keyboardOpen || c.keyboard)) {
                            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
                                c.tap = std::make_pair(event.button.x, event.button.y);
                            continue;
                        }
                        if (state.streaming && event.type == SDL_MOUSEMOTION &&
                            inputArea.contains(event.motion.x, event.motion.y))
                            movePointer(inputArea.position(event.motion.x, event.motion.y));
                        if (state.streaming &&
                            (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
                            (event.button.button == SDL_BUTTON_LEFT ||
                             event.button.button == SDL_BUTTON_RIGHT)) {
                            const auto bit = SDL_BUTTON(event.button.button);
                            const int button = event.button.button == SDL_BUTTON_LEFT ? 1 : 2;
                            if (event.type == SDL_MOUSEBUTTONDOWN && !(heldMouse & bit) &&
                                inputArea.contains(event.button.x, event.button.y)) {
                                movePointer(inputArea.position(event.button.x, event.button.y));
                                send({{"action", "mouse_press"}, {"button", button}});
                                heldMouse |= bit;
                            } else if (event.type == SDL_MOUSEBUTTONUP && (heldMouse & bit)) {
                                send({{"action", "mouse_release"}, {"button", button}});
                                heldMouse &= ~bit;
                            }
                        }
#endif
                    }
#ifdef __SWITCH__
                    padUpdate(&pad);
                    const uint64_t pressed = focused ? padGetButtonsDown(&pad) : 0;
                    c.held = padGetButtons(&pad);
                    c.confirm |= pressed & HidNpadButton_A;
                    c.back |= pressed & HidNpadButton_B;
                    c.refresh |= pressed & HidNpadButton_Y;
                    c.logout |= pressed & HidNpadButton_X;
                    c.up |= pressed & HidNpadButton_Up;
                    c.down |= pressed & HidNpadButton_Down;
                    c.left |= pressed & HidNpadButton_Left;
                    c.right |= pressed & HidNpadButton_Right;
                    c.help |= pressed & HidNpadButton_Plus;
                    c.disconnect |= pressed & HidNpadButton_Minus;
                    c.keyboard |= pressed & HidNpadButton_StickR;
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
                    bool busy = state.stage != "idle" && state.stage != "error" && state.stage != "login" &&
                                state.stage != "sms";
                    if (c.exit || !running)
                        break;
                    if (keyboardOpen && (!state.streaming || !focused)) {
                        keyboardOpen = false;
                        keyboard = {};
                    }
                    if (state.streaming) {
                        if (c.help)
                            overlay = !overlay;
                        if (c.disconnect) {
                            client.command("disconnect");
                            continue;
                        }
                        if (focused && (c.keyboard || (keyboardOpen && c.back))) {
                            keyboardOpen = !keyboardOpen;
                            keyboard = {};
                            client.command("release");
#ifdef __SWITCH__
                            controllerInput.reset();
#else
                            heldMouse = 0;
#endif
                        } else if (keyboardOpen && focused) {
#ifdef __SWITCH__
                            const auto touch = sampleController(controllerInput, 0, {0, 0, 1280, 720});
                            if (touch.leftChanged && touch.left && touch.touchMove)
                                c.tap = std::make_pair(int(std::lround(touch.touchMove->x * 1280)),
                                                       int(std::lround(touch.touchMove->y * 720)));
#endif
                            keyboard.move(int(c.right) - int(c.left), int(c.down) - int(c.up));
                            const bool tapped = c.tap && keyboard.selectAt(c.tap->first, c.tap->second);
                            if (c.confirm || tapped)
                                keyboard.press([&](int key, bool down) {
                                    send({{"action", down ? "kbd_press" : "kbd_release"}, {"key", key}});
                                });
                        } else if (focused) {
#ifdef __SWITCH__
                            const auto input = sampleController(controllerInput, c.held, inputArea);
                            if (input.leftChanged && !input.left)
                                send({{"action", "mouse_release"}, {"button", 1}});
                            if (input.touchMove)
                                movePointer(*input.touchMove);
                            if (!input.touchActive) {
                                cursorX = std::clamp(cursorX + c.mx * dt * .7f, 0.f, 1.f);
                                cursorY = std::clamp(cursorY + c.my * dt * .7f, 0.f, 1.f);
                                if ((c.mx || c.my) && now - lastPointer >= 16) {
                                    movePointer({cursorX, cursorY});
                                    lastPointer = now;
                                }
                            }
                            if (c.scroll && now - lastScroll > 80) {
                                send({{"action", "mouse_scroll"}, {"delta_x", 0},
                                      {"delta_y", int(c.scroll * 120)}});
                                lastScroll = now;
                            }
                            for (auto [button, keycode] : controllerKeys)
                                if (input.changed & button)
                                    send({{"action", (input.held & button) ? "kbd_press" : "kbd_release"},
                                          {"key", keycode}});
                            if (input.leftChanged && input.left)
                                send({{"action", "mouse_press"}, {"button", 1}});
                            if (input.changed & HidNpadButton_ZL)
                                send({{"action", (input.held & HidNpadButton_ZL) ? "mouse_press" : "mouse_release"},
                                      {"button", 2}});
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
                        }
                    } else {
                        if (wasStreaming) {
                            video.reset();
                            serial = 0;
                            texW = texH = 0;
                            viewport = {0, 0, 1280, 720};
                            overlay = true;
                        }
                        if (!state.authenticated && c.logout && (!busy || state.stage == "login")) {
                            sms = SmsForm{};
                            client.command(state.loginMode == "sms" ? "login" : "sms-open");
                        } else if (c.back || c.disconnect) {
                            sms = SmsForm{};
                            client.command("cancel");
                        } else if (c.logout && state.authenticated)
                            client.command("logout");
                        else if (c.refresh && state.authenticated && !busy)
                            client.command("refresh");
                        else if (!state.authenticated && state.loginMode == "sms") {
                            if (!busy) {
                                if (c.up)
                                    sms.selected = std::max(0, sms.selected - 1);
                                if (c.down)
                                    sms.selected = std::min(4, sms.selected + 1);
                                if (c.confirm) {
                                    if (sms.selected < 3) {
                                        auto *value = sms.selected == 0   ? &sms.country
                                                      : sms.selected == 1 ? &sms.mobile
                                                                          : &sms.code;
                                        const char *title = sms.selected == 0   ? "国家或地区区号"
                                                            : sms.selected == 1 ? "手机号码"
                                                                                : "6 位短信验证码";
                                        auto input = numericInput(title, *value,
                                                                  sms.selected == 0   ? 1
                                                                  : sms.selected == 1 ? 4
                                                                                      : 6,
                                                                  sms.selected == 0   ? 4
                                                                  : sms.selected == 1 ? 15
                                                                                      : 6);
                                        if (input) {
                                            *value = *input;
                                            sms.selected = sms.selected == 1 ? 3 : sms.selected == 2 ? 4 : 1;
                                        }
#ifdef __SWITCH__
                                        padUpdate(&pad);
#endif
                                    } else if (sms.selected == 3 && uu::monotonicMs() >= state.smsRetryAt) {
                                        client.command("sms-code", sms.credentials());
                                        sms.selected = 2;
                                    } else if (sms.selected == 4)
                                        client.command("sms-login", sms.credentials());
                                }
                            }
                        } else if (!busy && state.stage != "login") {
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
                    wasStreaming = state.streaming;
                    color(renderer, {13, 19, 25, 255});
                    SDL_RenderClear(renderer);
                    if (state.streaming) {
                        if (const auto picture = media->picture(serial)) {
                            const int width = picture->width, height = picture->height;
                            VideoTexture replacement(nullptr, SDL_DestroyTexture);
                            auto *target = video.get();
                            if (!video || width != texW || height != texH) {
                                replacement.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                                    SDL_TEXTUREACCESS_STREAMING, width,
                                                                    height));
                                target = replacement.get();
                            }
                            if (target && SDL_UpdateTexture(target, nullptr, picture->pixels.data(),
                                                            width * 4) == 0) {
                                if (replacement) {
                                    video = std::move(replacement);
                                    texW = width;
                                    texH = height;
                                    const float scale = std::min(1280.f / width, 720.f / height);
                                    viewport = {int((1280 - width * scale) / 2),
                                                int((720 - height * scale) / 2), int(width * scale),
                                                int(height * scale)};
                                }
                                if (!serial)
                                    uu::diagnostic("video-first-texture-uploaded");
                                serial = picture->serial;
                            }
                        }
                        if (video)
                            SDL_RenderCopy(renderer, video.get(), nullptr, &viewport);
                        else
                            text(renderer, fonts.normal, "等待电脑发送画面…", 470, 330, muted);
                        int x = viewport.x + int(cursorX * viewport.w),
                            y = viewport.y + int(cursorY * viewport.h);
                        rect(renderer, x - 5, y, 11, 2, accent);
                        rect(renderer, x, y - 5, 2, 11, accent);
                        if (overlay) {
                            rect(renderer, 0, 0, 1280, 68, {13, 19, 25, 225});
                            text(renderer, fonts.normal, "UU远程", 28, 18);
                            rect(renderer, 0, 650, 1280, 70, {13, 19, 25, 225});
#ifdef __SWITCH__
                            prompts.draw(renderer, fonts.small, 190, 18,
                                         {{ButtonIcon::LeftStick, "鼠标"}, {ButtonIcon::ZR, "点击", ButtonIcon::ZL},
                                          {ButtonIcon::A, "确认"}, {ButtonIcon::B, "Esc"},
                                          {ButtonIcon::Minus, "断开"}, {ButtonIcon::Plus, "隐藏"},
                                          {ButtonIcon::RightClick, "键盘"}});
                            prompts.draw(renderer, fonts.small, 26, 655,
                                         {{ButtonIcon::Dpad, "WASD"}, {ButtonIcon::X, "空格"},
                                          {ButtonIcon::Y, "Tab"}, {ButtonIcon::L, "Ctrl"},
                                          {ButtonIcon::R, "Shift"}, {ButtonIcon::RightStick, "滚轮"}});
#else
                            prompts.draw(renderer, fonts.small, 190, 18,
                                         {{ButtonIcon::F1, "隐藏"}, {ButtonIcon::F2, "断开"},
                                          {ButtonIcon::F3, "键盘"}, {confirmIcon, "确认"}, {backIcon, "返回"}});
#endif
                            text(renderer, fonts.small, media->status(), 28, 694, muted);
                        }
                        if (keyboardOpen) {
                            rect(renderer, 90, 286, 1100, 402, {13, 19, 25, 248});
                            text(renderer, fonts.normal, "远程键盘", 130, 304);
#ifdef __SWITCH__
                            constexpr auto keyboardIcon = ButtonIcon::RightClick;
#else
                            constexpr auto keyboardIcon = ButtonIcon::F3;
#endif
                            prompts.draw(renderer, fonts.small, 268, 302,
                                         {{ButtonIcon::Dpad, "选键"}, {confirmIcon, "输入"},
                                          {backIcon, "关闭", keyboardIcon}});
                            keyboardLabels.draw(renderer, fonts.small, keyboard);
                        }
                    } else {
                        text(renderer, fonts.large, "UU远程", 62, 44);
                        text(renderer, fonts.small, "NINTENDO SWITCH  v" UU_VERSION, 270, 69, accent);
                        text(renderer, fonts.small, state.authenticated ? state.nickname : "尚未登录", 930,
                             69, muted, 280);
                        rect(renderer, 62, 127, 1156, 1, {44, 56, 67, 255});
                        if (state.qrUrl != qrUrl) {
                            qrUrl = state.qrUrl;
                            qr.reset();
                            if (!qrUrl.empty())
                                qr = std::make_unique<qrcodegen::QrCode>(qrcodegen::QrCode::encodeText(
                                    qrUrl.c_str(), qrcodegen::QrCode::Ecc::MEDIUM));
                        }
                        if (!state.authenticated && state.loginMode == "sms") {
                            text(renderer, fonts.large, "短信验证码登录", 62, 162);
                            text(renderer, fonts.small, "使用与电脑端相同的手机号", 62, 220, muted);
                            int xs[] = {80, 310, 840}, widths[] = {190, 490, 340};
                            const char *labels[] = {"国家 / 地区", "手机号码", "短信验证码"};
                            std::string values[] = {
                                "+" + sms.country, sms.mobile.empty() ? "输入手机号码" : sms.mobile,
                                sms.code.empty() ? "输入 6 位验证码" : std::string(sms.code.size(), '*')};
                            for (int field = 0; field < 3; ++field) {
                                text(renderer, fonts.small, labels[field], xs[field], 278, muted);
                                rect(renderer, xs[field], 316, widths[field], 64,
                                     sms.selected == field ? SDL_Color{32, 60, 57, 255}
                                                           : SDL_Color{28, 40, 49, 255});
                                if (sms.selected == field)
                                    rect(renderer, xs[field], 316, 4, 64, accent);
                                text(renderer, fonts.normal, values[field], xs[field] + 18, 334, white,
                                     widths[field] - 30);
                            }
                            int remaining = int(
                                std::max<int64_t>(0, (state.smsRetryAt - uu::monotonicMs() + 999) / 1000));
                            rect(renderer, 80, 424, 500, 68,
                                 sms.selected == 3 ? accent : SDL_Color{28, 40, 49, 255});
                            rect(renderer, 660, 424, 520, 68,
                                 sms.selected == 4 ? accent : SDL_Color{28, 40, 49, 255});
                            text(renderer, fonts.normal,
                                 remaining ? std::to_string(remaining) + " 秒后重发" : "发送验证码", 104, 444,
                                 sms.selected == 3 ? SDL_Color{13, 19, 25, 255} : white);
                            text(renderer, fonts.normal, "登录", 686, 444,
                                 sms.selected == 4 ? SDL_Color{13, 19, 25, 255} : white);
                            prompts.draw(renderer, fonts.small, 80, 507,
                                         {{ButtonIcon::UpDown, "选择项目"}, {confirmIcon, "输入或确认"},
                                          {ButtonIcon::X, "切换扫码登录"}});
                        } else if (state.stage == "login") {
                            text(renderer, fonts.large, "扫码登录", 62, 208);
                            text(renderer, fonts.normal, "使用手机 UU远程 App", 62, 294, white, 550);
                            text(renderer, fonts.normal, "扫描右侧二维码并确认", 62, 336, white, 550);
                            prompts.draw(renderer, fonts.small, 62, 426, {{ButtonIcon::X, "短信验证码登录"}}, accent);
                            prompts.draw(renderer, fonts.small, 62, 470, {{backIcon, "取消 / 返回"}});
                            if (qr) {
                                auto layout = uu::qrGeometry(qr->getSize());
                                rect(renderer, layout.x, layout.y, layout.size, layout.size,
                                     {255, 255, 255, 255});
                                for (int y = 0; y < qr->getSize(); ++y)
                                    for (int x = 0; x < qr->getSize(); ++x)
                                        if (qr->getModule(x, y))
                                            rect(renderer, layout.x + (x + layout.border) * layout.scale,
                                                 layout.y + (y + layout.border) * layout.scale, layout.scale,
                                                 layout.scale, {0, 0, 0, 255});
                            } else
                                text(renderer, fonts.normal, "正在获取二维码…", 760, 360, muted);
                        } else {
                            text(renderer, fonts.small, "我的电脑", 62, 167, muted);
                            if (!state.devices.empty()) {
                                int first = std::max(0, selected - 3);
                                float target = 223.f + (selected - first) * 88;
                                focusY += (target - focusY) * std::min(1.f, dt * 18);
                                rect(renderer, 62, int(focusY), 535, 78, {32, 50, 53, 255});
                                rect(renderer, 62, int(focusY), 4, 78, accent);
                                for (int i = first; i < std::min(first + 4, int(state.devices.size())); ++i) {
                                    auto &device = state.devices[i];
                                    int y = 232 + (i - first) * 88;
                                    bool online = device.value("status", std::string{}) != "offline";
                                    text(renderer, fonts.normal, device.value("alias", std::string("电脑")),
                                         84, y, white, 470);
                                    text(renderer, fonts.small, online ? "在线" : "离线", 84, y + 35,
                                         online ? accent : muted);
                                }
                            } else {
                                text(renderer, fonts.normal,
                                     state.authenticated ? "暂未发现电脑" : "登录你的 UU 账号", 62, 237);
                                text(renderer, fonts.small,
                                     state.authenticated ? "请在电脑上登录同一账号"
                                                         : "扫码或短信登录，同步远程电脑",
                                     62, 287, muted);
                                rect(renderer, 62, 350, 440, 64, accent);
                                prompts.draw(renderer, fonts.normal, 84, 366,
                                             {{state.authenticated ? ButtonIcon::Y : confirmIcon,
                                               state.authenticated ? "刷新设备" : "扫码登录"}}, {13, 19, 25, 255});
                                if (!state.authenticated)
                                    prompts.draw(renderer, fonts.normal, 62, 446, {{ButtonIcon::X, "短信验证码登录"}}, accent);
                            }
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
                        if (state.stage == "login")
                            text(renderer, fonts.small, state.detail, 82, 548, muted, 550);
                        else
                            statusText(renderer, fonts.small, state.detail,
                                       state.stage == "error" ? SDL_Color{255, 157, 141, 255} : muted);
                        rect(renderer, 62, 653, 1156, 1, {44, 56, 67, 255});
                        if (state.authenticated)
                            prompts.draw(renderer, fonts.small, 62, 667,
                                         {{confirmIcon, "确认"}, {backIcon, "返回"}, {ButtonIcon::Y, "刷新"},
                                          {ButtonIcon::X, "退出账号"}});
                        else
                            prompts.draw(renderer, fonts.small, 62, 667,
                                         {{confirmIcon, "确认"}, {backIcon, "返回"}, {ButtonIcon::X, "切换登录方式"}});
                        prompts.draw(renderer, fonts.small, 1100, 667, {{exitIcon, "退出"}});
                    }
                    SDL_RenderPresent(renderer);
                }
            }
            rtc::Cleanup().wait();
            uu::waitBackgroundTasks();
            media.reset();
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
        }
        curl_global_cleanup();
        TTF_Quit();
        SDL_Quit();
    } catch (const std::exception &e) {
        result = 1;
        uu::diagnosticException("main-exception", std::current_exception());
        try {
            rtc::Cleanup().wait();
            uu::waitBackgroundTasks();
        } catch (...) {
            uu::diagnosticException("cleanup-exception", std::current_exception());
        }
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
    nifmExit();
    socketExit();
#endif
    return result;
}
