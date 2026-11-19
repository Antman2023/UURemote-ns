#pragma once

#include "controller_input.hpp"
#include <array>
#include <cstdlib>
#include <iterator>

namespace uu {
struct RemoteKey {
    const char *label;
    int code;
    int width = 68;
};

class RemoteKeyboard {
  public:
    struct Row {
        const RemoteKey *keys;
        int count;
    };

  private:
    // Windows virtual-key codes used by the existing control channel.
    static inline constexpr RemoteKey numbers[] = {
        {"Esc", 0x1B, 88}, {"1 !", '1'}, {"2 @", '2'}, {"3 #", '3'}, {"4 $", '4'}, {"5 %", '5'},
        {"6 ^", '6'}, {"7 &", '7'}, {"8 *", '8'}, {"9 (", '9'}, {"0 )", '0'}, {"- _", 0xBD},
        {"= +", 0xBB}, {"退格", 0x08, 108},
    };
    static inline constexpr RemoteKey top[] = {
        {"Tab", 0x09, 92}, {"Q", 'Q'}, {"W", 'W'}, {"E", 'E'}, {"R", 'R'}, {"T", 'T'}, {"Y", 'Y'},
        {"U", 'U'}, {"I", 'I'}, {"O", 'O'}, {"P", 'P'}, {"[ {", 0xDB}, {"] }", 0xDD}, {"\\ |", 0xDC},
    };
    static inline constexpr RemoteKey home[] = {
        {"Caps", 0x14, 112}, {"A", 'A'}, {"S", 'S'}, {"D", 'D'}, {"F", 'F'}, {"G", 'G'}, {"H", 'H'},
        {"J", 'J'}, {"K", 'K'}, {"L", 'L'}, {"; :", 0xBA}, {"' \"", 0xDE}, {"Enter", 0x0D, 116},
    };
    static inline constexpr RemoteKey bottom[] = {
        {"Shift", 0x10, 124}, {"Z", 'Z'}, {"X", 'X'}, {"C", 'C'}, {"V", 'V'}, {"B", 'B'}, {"N", 'N'},
        {"M", 'M'}, {", <", 0xBC}, {". >", 0xBE}, {"/ ?", 0xBF}, {"` ~", 0xC0}, {"↑", 0x26},
    };
    static inline constexpr RemoteKey space[] = {
        {"Ctrl", 0x11, 112}, {"Alt", 0x12, 100}, {"空格", 0x20, 356}, {"Delete", 0x2E, 112},
        {"←", 0x25}, {"↓", 0x28}, {"→", 0x27},
    };
    static inline constexpr std::array<int, 3> modifierCodes{0x11, 0x12, 0x10};
    std::array<bool, modifierCodes.size()> modifiers_{};
    int row_ = 1, column_ = 1;

  public:
    static inline constexpr Row rows[] = {
        {numbers, int(std::size(numbers))}, {top, int(std::size(top))}, {home, int(std::size(home))},
        {bottom, int(std::size(bottom))}, {space, int(std::size(space))},
    };

    static InputArea bounds(int row, int column) {
        const auto &r = rows[row];
        int width = 0;
        for (int i = 0; i < r.count; ++i)
            width += r.keys[i].width;
        int x = (1280 - width) / 2;
        for (int i = 0; i < column; ++i)
            x += r.keys[i].width;
        return {x, 350 + row * 60, r.keys[column].width - 4, 56};
    }

    bool selected(int row, int column) const { return row == row_ && column == column_; }
    bool armed(int code) const {
        for (std::size_t i = 0; i < modifierCodes.size(); ++i)
            if (modifierCodes[i] == code)
                return modifiers_[i];
        return false;
    }

    void move(int dx, int dy) {
        const int nextRow = std::clamp(row_ + dy, 0, int(std::size(rows)) - 1);
        if (nextRow != row_) {
            const auto previous = bounds(row_, column_);
            const int center = previous.x + previous.width / 2;
            int distance = 1280;
            row_ = nextRow;
            for (int i = 0; i < rows[row_].count; ++i) {
                const auto candidate = bounds(row_, i);
                const int offset = std::abs(candidate.x + candidate.width / 2 - center);
                if (offset < distance) {
                    distance = offset;
                    column_ = i;
                }
            }
        }
        column_ = std::clamp(column_ + dx, 0, rows[row_].count - 1);
    }

    bool selectAt(int x, int y) {
        for (int row = 0; row < int(std::size(rows)); ++row)
            for (int column = 0; column < rows[row].count; ++column)
                if (bounds(row, column).contains(x, y)) {
                    row_ = row;
                    column_ = column;
                    return true;
                }
        return false;
    }

    template <typename Send> void press(const Send &send) {
        const int code = rows[row_].keys[column_].code;
        for (std::size_t i = 0; i < modifierCodes.size(); ++i)
            if (modifierCodes[i] == code) {
                modifiers_[i] = !modifiers_[i];
                return;
            }
        for (std::size_t i = 0; i < modifierCodes.size(); ++i)
            if (modifiers_[i])
                send(modifierCodes[i], true);
        send(code, true);
        send(code, false);
        for (std::size_t i = modifierCodes.size(); i > 0; --i)
            if (modifiers_[i - 1])
                send(modifierCodes[i - 1], false);
        modifiers_.fill(false);
    }
};
} // namespace uu
