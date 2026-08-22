#pragma once

#include <glaze/glaze.hpp>
#include <algorithm>
#include <concepts>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

enum class KeybindType { Hold = 0, Toggle, Override };

struct RawKeybind {
    int m_key;
    int m_modifiers;
    std::string m_valueTag;

    KeybindType m_type;
    bool m_active = true;
    std::string m_value;
};

namespace grape::keybinds {
inline constexpr int SHIFT = 0b001;
inline constexpr int CTRL = 0b010;
inline constexpr int ALT = 0b100;
inline constexpr int FRAME_ADVANCE_KEY = 'C';
inline constexpr int ADVANCE_ONE_KEY = 'V';

inline constexpr int modifierMask(bool ctrl, bool shift, bool alt) {
    return (ctrl ? CTRL : 0) | (shift ? SHIFT : 0) | (alt ? ALT : 0);
}

inline constexpr int normalizeModifiers(int key, int modifiers) {
    if (key == 0x11) modifiers &= ~CTRL;
    if (key == 0x10) modifiers &= ~SHIFT;
    if (key == 0x12) modifiers &= ~ALT;
    return modifiers;
}

inline void migrateLegacy(RawKeybind& keybind) {
    if (keybind.m_key == 0x12 && keybind.m_modifiers == SHIFT &&
        keybind.m_valueTag == "ui.visible") {
        keybind.m_modifiers = 0;
    }
    keybind.m_modifiers =
        normalizeModifiers(keybind.m_key, keybind.m_modifiers);
}
}  // namespace grape::keybinds

class KeyPressTracker {
    std::unordered_map<int, int> m_pressed;

   public:
    bool resolve(int key, bool pressed, int& hash) {
        if (pressed) {
            auto [active, inserted] = m_pressed.try_emplace(key, hash);
            if (!inserted) return false;
            hash = active->second;
        } else if (const auto active = m_pressed.find(key);
                   active != m_pressed.end()) {
            hash = active->second;
            m_pressed.erase(active);
        }
        return true;
    }
};

template <>
struct glz::meta<RawKeybind> {
    using T = RawKeybind;
    static constexpr auto value = object(
        "key", &T::m_key,
        "modifiers", &T::m_modifiers,
        "tag", &T::m_valueTag,
        "type", &T::m_type,
        "value", &T::m_value,
        "active", &T::m_active
    );
};

class KeybindControl {
   protected:
    int m_key;
    int m_modifiers;
    std::string m_valueTag;

    KeybindType m_type;
    bool m_enabled = false;
    bool m_active = true;

   public:
    using HashT = int;

    virtual HashT getHash() const = 0;
    virtual const std::string& getTag() const = 0;
    virtual bool applyValue(bool pressed, void* value, void* previous) = 0;
    virtual void fromString(const std::string& str) = 0;
    virtual std::string toString() const = 0;

    virtual ~KeybindControl() = default;

    friend glz::meta<KeybindControl>;
};

template <typename T>
class GrapeKeybind : public KeybindControl {
    using Self = GrapeKeybind<T>;

    struct HoldState {
        T original;
        std::vector<Self*> active;
    };
    inline static std::unordered_map<T*, HoldState> s_holds;

    T* m_holdValue = nullptr;
    T* m_holdPrevious = nullptr;

    bool releaseHold() {
        if (!m_enabled || !m_holdValue) return false;

        const auto stateIt = s_holds.find(m_holdValue);
        if (stateIt == s_holds.end()) {
            m_enabled = false;
            m_holdValue = nullptr;
            m_holdPrevious = nullptr;
            return false;
        }

        auto& state = stateIt->second;
        const bool wasTop = !state.active.empty() && state.active.back() == this;
        state.active.erase(
            std::remove(state.active.begin(), state.active.end(), this),
            state.active.end());

        bool changed = false;
        if (wasTop) {
            const T next = state.active.empty()
                ? state.original : state.active.back()->m_value;
            changed = *m_holdValue != next;
            *m_holdValue = next;
        }
        if (state.active.empty()) {
            if (m_holdPrevious) *m_holdPrevious = m_value;
            s_holds.erase(stateIt);
        }

        m_enabled = false;
        m_holdValue = nullptr;
        m_holdPrevious = nullptr;
        return changed;
    }

   public:
    /**
     * Behavior explanation:
     *
     * ## Hold
     * - The keybind will only be active while the key is being held down
     * - The keybind will be disabled when the key is released
     * - If the value is already set to the keybind's value, it won't be changed
     *
     * ## Toggle
     * - The keybind will be activated when the key is pressed
     * - The keybind will be deactivated when the key is pressed again
     * - If the value is already set to the keybind's value, it'll go back to
     * the previous known value
     *
     * ## Override
     * - Set the value on key press, but don't change it back
     */

    static constexpr int MODIFIER_SHIFT = grape::keybinds::SHIFT;
    static constexpr int MODIFIER_CTRL = grape::keybinds::CTRL;
    static constexpr int MODIFIER_ALT = grape::keybinds::ALT;
    static_assert((MODIFIER_SHIFT | MODIFIER_CTRL | MODIFIER_ALT) == 0b111);

   private:
    T m_value;

   public:
    GrapeKeybind(int key, int modifiers, KeybindType type, T value,
                 std::string tag, bool active = true)
        : m_value(value) {
        m_key = key;
        m_modifiers = modifiers;
        m_valueTag = tag;
        m_type = type;
        m_active = active;
    }

    static std::shared_ptr<GrapeKeybind<T>> create(std::string tag, int key,
                                                KeybindType type, T value,
                                                int modifiers = 0) {
        return std::make_shared<GrapeKeybind<T>>(key, modifiers, type, value, tag);
    }

    static std::shared_ptr<GrapeKeybind<T>> createFromString(
        std::string tag, int key, KeybindType type, std::string value,
        int modifiers = 0, bool active = true) {
        return std::make_shared<GrapeKeybind<T>>(
            key, modifiers, type, Self::readFromString(value), tag, active);
    }

    HashT getHash() const override { return m_key | (m_modifiers << 20); }

    const std::string& getTag() const override { return m_valueTag; }

    bool applyValue(bool pressed, void* value, void* previous) override {
        if (!m_active) return false;

        T& valueRef = *static_cast<T*>(value);
        T& previousRef = *static_cast<T*>(previous);

        switch (m_type) {
            case KeybindType::Toggle:
                if (!pressed) return false;
                if (valueRef == m_value) {
                    valueRef = previousRef;
                } else {
                    previousRef = valueRef;
                    valueRef = m_value;
                }
                return true;

            case KeybindType::Hold:
                if (pressed) {
                    if (m_enabled) return false;
                    auto [state, inserted] = s_holds.try_emplace(
                        &valueRef, HoldState{valueRef, {}});
                    if (inserted) previousRef = valueRef;
                    state->second.active.push_back(this);
                    m_holdValue = &valueRef;
                    m_holdPrevious = &previousRef;
                    m_enabled = true;
                    const bool changed = valueRef != m_value;
                    valueRef = m_value;
                    return changed;
                }
                return releaseHold();

            case KeybindType::Override:
                if (!pressed) return false;
                previousRef = valueRef;
                valueRef = m_value;
                return true;
        }
        return false;
    }

    static T readFromString(const std::string& str) {
        if constexpr (std::same_as<T, std::string>) return str;
        T value{};
        std::istringstream(str) >> value;
        return value;
    }

    void fromString(const std::string& str) override {
        m_value = readFromString(str);
    }

    std::string toString() const override {
        if constexpr (std::same_as<T, std::string>) return m_value;
        std::ostringstream output;
        output << m_value;
        return output.str();
    }

    friend glz::meta<GrapeKeybind<T>>;
};

template <>
struct glz::meta<KeybindControl> {
    using T = KeybindControl;
    static constexpr auto value = object(
        "key", &T::m_key,
        "modifiers", &T::m_modifiers,
        "tag", &T::m_valueTag,
        "type", &T::m_type,
        "value", custom<&T::fromString, &T::toString>,
        "active", &T::m_active,
        "enabled", hide{&T::m_enabled}
    );
};

template <typename T>
using KeybindPtr = std::shared_ptr<GrapeKeybind<T>>;
