#pragma once

#include <glaze/glaze.hpp>
#include <memory>
#include <sstream>

enum class KeybindType { Hold = 0, Toggle, Override };

struct RawKeybind {
    int m_key;
    int m_modifiers;
    std::string m_valueTag;

    KeybindType m_type;
    bool m_active = true;
    std::string m_value;
};

// clang-format off
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
// clang-format on

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

    virtual HashT getHash() = 0;
    const virtual std::string& getTag() = 0;
    virtual void applyValue(bool pressed, void* value, void* previous) = 0;
    virtual void fromString(const std::string& str) = 0;
    const virtual std::string toString() = 0;

    virtual ~KeybindControl() = default;

    friend glz::meta<KeybindControl>;
};

template <typename T>
class SLKeybind : public KeybindControl {
    using Self = SLKeybind<T>;

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

    static constexpr bool MODIFIER_SHIFT = 0b001;
    static constexpr bool MODIFIER_CTRL = 0b010;
    static constexpr bool MODIFIER_ALT = 0b100;

   private:
    // What value to set when the keybind is pressed
    T m_value;

   public:
    SLKeybind(int key, int modifiers, KeybindType type, T value,
              std::string tag)
        : m_value(value) {
        m_key = key;
        m_modifiers = modifiers;
        m_valueTag = tag;
        m_type = type;
    }

    static std::shared_ptr<SLKeybind<T>> create(std::string tag, int key,
                                                KeybindType type, T value,
                                                int modifiers = 0) {
        return std::make_shared<SLKeybind<T>>(key, modifiers, type, value, tag);
    }

    static std::shared_ptr<SLKeybind<T>> createFromString(std::string tag,
                                                          int key,
                                                          KeybindType type,
                                                          std::string value,
                                                          int modifiers = 0) {
        return std::make_shared<SLKeybind<T>>(key, modifiers, type,
                                              Self::readFromString(value), tag);
    }

    HashT getHash() override { return m_key | (m_modifiers << 20); }

    const std::string& getTag() override { return m_valueTag; }

    void applyValue(bool pressed, void* value, void* previous) override {
        if (!m_active) return;

        T& valueRef = *(T*)value;
        T& previousRef = *(T*)previous;

        switch (m_type) {
            case KeybindType::Toggle: {
                if (!pressed) return;

                if (valueRef == m_value) {
                    valueRef = previousRef;
                } else {
                    previousRef = valueRef;
                    valueRef = m_value;
                }

                break;
            }
            case KeybindType::Hold: {
                if (!m_enabled && pressed) {
                    previousRef = valueRef;
                    valueRef = m_value;
                    m_enabled = true;
                } else if (m_enabled && !pressed) {
                    valueRef = previousRef;
                    previousRef = m_value;
                    m_enabled = false;
                }

                break;
            }
            case KeybindType::Override: {
                previousRef = valueRef;
                valueRef = m_value;
                break;
            }
        }
    }

    static T readFromString(const std::string& str) {
        std::istringstream iss(str);
        T value;
        iss >> value;
        return value;
    }

    void fromString(const std::string& str) override {
        std::istringstream iss(str);
        iss >> m_value;
    }

    const std::string toString() override {
        std::ostringstream oss;
        oss << m_value;
        return oss.str();
    }

    friend glz::meta<SLKeybind<T>>;
};

// clang-format off
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
// clang-format on

template <typename T>
using KeybindPtr = std::shared_ptr<SLKeybind<T>>;

template <typename T>
class KeybindContainer {
   protected:
    std::string m_tag;
    std::vector<SLKeybind<T>> m_keybinds;

   public:
    std::vector<SLKeybind<T>>& getKeybinds() { return m_keybinds; }
};
