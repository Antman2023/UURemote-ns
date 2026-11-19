#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace uu {
struct PointerPosition {
    float x, y;
};

struct InputArea {
    int x = 0, y = 0, width = 0, height = 0;

    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    PointerPosition position(int px, int py) const {
        return {(px - x) / float(width), (py - y) / float(height)};
    }
};

struct TouchContact {
    uint32_t id;
    int x, y;
};

struct ControllerSample {
    uint64_t held, changed;
    bool left, leftChanged, touchActive;
    std::optional<PointerPosition> touchMove;
};

class ControllerInput {
    struct Drag {
        uint32_t finger;
        PointerPosition position;
    };
    std::optional<Drag> drag_;
    uint64_t held_ = 0, blockedButtons_ = 0;
    bool active_ = false, touchBlocked_ = true, left_ = false;

    std::optional<PointerPosition> sampleTouch(const TouchContact *touches, std::size_t count,
                                               InputArea area) {
        if (!count) {
            drag_.reset();
            touchBlocked_ = false;
            return std::nullopt;
        }
        if (touchBlocked_)
            return std::nullopt;

        const auto end = touches + count;
        auto contact = touches;
        if (drag_)
            contact = std::find_if(touches, end, [&](const TouchContact &touch) {
                return touch.id == drag_->finger;
            });
        if (contact == end || !area.contains(contact->x, contact->y)) {
            // Wait for all fingers to lift after leaving the picture or losing the drag's finger.
            drag_.reset();
            touchBlocked_ = true;
            return std::nullopt;
        }
        const auto position = area.position(contact->x, contact->y);
        if (drag_ && drag_->position.x == position.x && drag_->position.y == position.y)
            return std::nullopt;
        drag_ = Drag{contact->id, position};
        return position;
    }

  public:
    // The caller releases remote input when suspending a session. Ignore held controls on resume.
    void reset() { *this = {}; }

    ControllerSample sample(uint64_t buttons, uint64_t leftButton, const TouchContact *touches,
                            std::size_t count, InputArea area) {
        if (!active_) {
            blockedButtons_ = buttons;
            active_ = true;
        }
        blockedButtons_ &= buttons;
        const auto held = buttons & ~blockedButtons_;
        auto move = sampleTouch(touches, count, area);
        const bool touchActive = drag_.has_value();
        const bool left = touchActive || (held & leftButton);
        ControllerSample result{held, held ^ held_, left, left != left_, touchActive, move};
        held_ = held;
        left_ = left;
        return result;
    }
};
} // namespace uu
