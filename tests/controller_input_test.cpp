#include "controller_input.hpp"
#include "remote_keyboard.hpp"
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr uint64_t zr = 1, key = 2;
constexpr uu::InputArea picture{100, 0, 800, 600};
constexpr uu::TouchContact first{7, 500, 300}, second{12, 300, 150};

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

uu::ControllerSample sample(uu::ControllerInput &input, uint64_t buttons,
                            std::initializer_list<uu::TouchContact> touches = {}) {
    return input.sample(buttons, zr, touches.begin(), touches.size(), picture);
}

void leftButtonSources() {
    uu::ControllerInput input;
    sample(input, 0);
    auto state = sample(input, zr);
    check(state.left && state.leftChanged, "ZR did not press left mouse");
    state = sample(input, zr, {first});
    check(state.left && !state.leftChanged && state.touchMove, "Touch sent a second left press");
    state = sample(input, 0, {first});
    check(state.left && !state.leftChanged && !state.touchMove, "Releasing ZR interrupted the drag");
    state = sample(input, 0);
    check(!state.left && state.leftChanged, "Lifting the last source did not release left mouse");

    sample(input, 0, {first});
    sample(input, zr, {first});
    state = sample(input, zr);
    check(state.left && !state.leftChanged, "Lifting a finger released a held ZR");
    state = sample(input, 0);
    check(!state.left && state.leftChanged, "ZR did not release after the finger lifted");
}

void dragContactAndBounds() {
    uu::ControllerInput input;
    sample(input, 0);
    auto state = sample(input, 0, {first});
    check(state.touchMove && state.touchMove->x == .5f && state.touchMove->y == .5f,
          "Touch position did not match the video bounds");
    state = sample(input, 0, {second, first});
    check(state.touchActive && !state.touchMove, "A different finger moved the cursor");
    state = sample(input, 0, {second});
    check(!state.left && state.leftChanged, "The original finger did not end its drag");
    state = sample(input, 0, {second});
    check(!state.left && !state.touchMove, "A remaining finger started a replacement drag");

    sample(input, 0);
    state = sample(input, 0, {{first.id, 99, 300}});
    check(!state.left && !state.touchMove, "A touch in the black bar reached the remote");
    state = sample(input, 0, {first});
    check(!state.left && !state.touchMove, "A touch starting in the black bar began a drag");
    sample(input, 0);
    sample(input, 0, {first});
    state = sample(input, 0, {{first.id, 900, 300}});
    check(!state.left && state.leftChanged && !state.touchMove, "Leaving the picture did not release the drag");
}

void resumeAfterRelease() {
    uu::ControllerInput input;
    sample(input, 0);
    sample(input, zr | key, {first});
    input.reset();
    auto state = sample(input, zr | key, {first});
    check(state.held == 0 && state.changed == 0 && !state.left && !state.touchMove,
          "Resuming replayed held input");
    state = sample(input, 0);
    check(state.changed == 0 && !state.leftChanged, "Resuming created an unmatched release");
    state = sample(input, zr | key, {first});
    check(state.held == (zr | key) && state.changed == state.held && state.leftChanged && state.touchMove,
          "Controls stayed blocked after being released");
}

void remoteKeyboard() {
    uu::RemoteKeyboard keyboard;
    std::vector<std::pair<int, bool>> events;
    const auto send = [&](int code, bool down) { events.emplace_back(code, down); };
    const auto tap = [&](int row, int column) {
        const auto key = uu::RemoteKeyboard::bounds(row, column);
        check(keyboard.selectAt(key.x + key.width / 2, key.y + key.height / 2), "Key touch missed its cell");
        keyboard.press(send);
    };

    keyboard.move(1, 0);
    keyboard.press(send);
    check(events == std::vector<std::pair<int, bool>>{{'W', true}, {'W', false}}, "Arrow navigation selected the wrong key");
    events.clear();
    tap(4, 0); // Ctrl
    tap(3, 0); // Shift
    check(events.empty() && keyboard.armed(0x11) && keyboard.armed(0x10),
          "Choosing modifiers sent a premature key press");
    tap(2, 1); // A
    check(events == std::vector<std::pair<int, bool>>{{0x11, true}, {0x10, true}, {'A', true},
                                                     {'A', false}, {0x10, false}, {0x11, false}},
          "Modified key did not release every pressed key in order");
    check(!keyboard.armed(0x11) && !keyboard.armed(0x10), "Modifiers leaked into the next key");
    check(!keyboard.selectAt(10, 10), "A touch outside the keyboard selected a key");
}
} // namespace

int main() {
    try {
        leftButtonSources();
        dragContactAndBounds();
        resumeAfterRelease();
        remoteKeyboard();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
