#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <glaze/json/read.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "keybind.hpp"

struct BindingInterface {
    const virtual std::string& getId() = 0;

    virtual void* getValue() = 0;
    virtual void* getPrevious() = 0;

    virtual void notifyChange() = 0;
    virtual std::shared_ptr<KeybindControl> createKeybind(RawKeybind& kb) = 0;

    virtual ~BindingInterface() = default;
};

class BindingManager {
    std::unordered_map<std::string, std::shared_ptr<BindingInterface>>
        m_values;

    using KeybindVector = std::vector<std::shared_ptr<KeybindControl>>;
    std::unordered_map<KeybindControl::HashT, KeybindVector> m_keybinds;

    BindingManager() = default;

    bool m_needsNewKey  = false;
    bool m_keyReceived  = false;
    cocos2d::enumKeyCodes m_newKey = cocos2d::enumKeyCodes::KEY_None;

    bool m_hasRead = false;

   public:
    static BindingManager* get() {
        static BindingManager instance;
        return &instance;
    }

    void writeToFile(const std::filesystem::path& path) {
        KeybindVector vec;
        for (auto it = m_keybinds.begin(); it != m_keybinds.end(); ++it) {
            for (auto& kb : it->second) {
                vec.push_back(kb);
            }
        }

        std::string buf;
        (void)glz::write<glz::opts{.prettify = true}>(vec, buf);

        std::ofstream fd(path);
        fd << buf;
    }

    void readFromFile(const std::filesystem::path& path) {
        if (m_hasRead) return;
        m_hasRead = true;

        std::vector<RawKeybind> vec;
        auto ec = glz::read_file_json(vec, path.string(), std::string{});
        if (ec) {
            std::string helpful = glz::format_error(ec, std::string{});
            geode::log::error(
                "Failed to read keybinds: {}, assuming default keybinds",
                helpful);

            {
                auto kb = RawKeybind{
                    70, 0, "ui.visible", KeybindType::Toggle, true, "1"};
                this->registerKeybind(
                    m_values["ui.visible"]->createKeybind(kb));
            }

            {
                auto kb = RawKeybind{
                    67,   0,  "updater.frame_advance", KeybindType::Toggle,
                    true, "1"};
                this->registerKeybind(
                    m_values["updater.frame_advance"]->createKeybind(kb));
            }

            {
                auto kb = RawKeybind{
                    66,   0,  "updater.advance_back", KeybindType::Toggle,
                    true, "1"};
                this->registerKeybind(
                    m_values["updater.advance_back"]->createKeybind(kb));
            }

            {
                auto kb = RawKeybind{
                    86,   0,  "updater.advance_one", KeybindType::Toggle,
                    true, "1"};
                this->registerKeybind(
                    m_values["updater.advance_one"]->createKeybind(kb));
            }

            {
                auto kb = RawKeybind{
                    84,   0,  "trajectory.enabled", KeybindType::Toggle,
                    true, "1"};
                this->registerKeybind(
                    m_values["trajectory.enabled"]->createKeybind(kb));
            }

            {
                auto kb = RawKeybind{
                    18, 1, "ui.visible", KeybindType::Toggle, true, "1"};
                this->registerKeybind(
                    m_values["ui.visible"]->createKeybind(kb));
            }

            return;
        } else {
            geode::log::info("Read keybinds successfully!");
        }

        for (auto& kb : vec) {
            if (!m_values.contains(kb.m_valueTag)) {
                geode::log::error(
                    "Failed to register keybind for tag {}. Does it exist?",
                    kb.m_valueTag);
                continue;
            }

            auto value = m_values[kb.m_valueTag];
            this->registerKeybind(value->createKeybind(kb));
        }
    }

    void wantNewKey() {
        m_needsNewKey = true;
        m_keyReceived = false;
        m_newKey = cocos2d::enumKeyCodes::KEY_None;
    }

    void setNewKey(cocos2d::enumKeyCodes key) {
        m_newKey      = key;
        m_keyReceived = true;
        m_needsNewKey = false;
    }

    cocos2d::enumKeyCodes getNewKey() {
        if (m_keyReceived) {
            m_keyReceived = false;
            return m_newKey;
        }
        return cocos2d::enumKeyCodes::KEY_None;
    }

    bool isWaitingForKey() const { return m_needsNewKey; }
    bool hasNewKey()       const { return m_keyReceived;  }

    std::vector<std::string> getValueTags() const {
        std::vector<std::string> tags;
        tags.reserve(m_values.size());
        for (const auto& [tag, _] : m_values) tags.push_back(tag);
        std::sort(tags.begin(), tags.end());
        return tags;
    }

    std::vector<std::shared_ptr<KeybindControl>> getKeybindsForTag(
            const std::string& tag) const {
        std::vector<std::shared_ptr<KeybindControl>> result;
        for (const auto& [hash, vec] : m_keybinds) {
            for (const auto& kb : vec) {
                if (kb->getTag() == tag) result.push_back(kb);
            }
        }
        return result;
    }

    void removeKeybind(const std::shared_ptr<KeybindControl>& kb) {
        auto hash = kb->getHash();
        if (!m_keybinds.contains(hash)) return;
        auto& vec = m_keybinds[hash];
        vec.erase(std::remove(vec.begin(), vec.end(), kb), vec.end());
        if (vec.empty()) m_keybinds.erase(hash);
    }

    void removeAllKeybindsForTag(const std::string& tag) {
        for (auto& [hash, vec] : m_keybinds) {
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [&](const auto& kb) { return kb->getTag() == tag; }),
                vec.end());
        }
        
        for (auto it = m_keybinds.begin(); it != m_keybinds.end(); ) {
            if (it->second.empty()) it = m_keybinds.erase(it);
            else ++it;
        }
    }

    bool hasValue(const std::string& tag) const {
        return m_values.contains(tag);
    }

    bool addKeybindForTag(const std::string& tag, RawKeybind& raw) {
        if (!m_values.contains(tag)) return false;
        auto kb = m_values[tag]->createKeybind(raw);
        registerKeybind(kb);
        return true;
    }

    BindingManager(BindingManager const&) = delete;
    void operator=(BindingManager const&) = delete;

    void registerValue(std::shared_ptr<BindingInterface> value) {
        m_values[value->getId()] = value;
    }

    void registerKeybind(std::shared_ptr<KeybindControl> kb) {
        geode::log::info("Registering new keybind for {}", kb->getTag());
        if (m_keybinds.contains(kb->getHash())) {
            m_keybinds[kb->getHash()].push_back(kb);
        } else {
            m_keybinds[kb->getHash()] = {kb};
        }
    }

    void processKeyEvent(int key, bool pressed, bool ctrl, bool shift,
                         bool alt) {
        int modifiers = 0;
        if (ctrl) modifiers |= GrapeKeybind<void*>::MODIFIER_CTRL;
        if (shift) modifiers |= GrapeKeybind<void*>::MODIFIER_SHIFT;
        if (alt) modifiers |= GrapeKeybind<void*>::MODIFIER_ALT;
        KeybindControl::HashT hash = key | (modifiers << 20);

        if (!m_keybinds.contains(hash)) return;
        for (const auto& keybind : m_keybinds[hash]) {
            if (!m_values.contains(keybind->getTag())) return;

            auto value = m_values[keybind->getTag()];
            keybind->applyValue(pressed, value->getValue(),
                                value->getPrevious());
            value->notifyChange();
        }
    }
};

template <typename T>
struct ConfigValuePtr;

template <typename T>
class ConfigValue : public KeybindContainer<T>, public BindingInterface {
   private:
    T* m_value;
    T m_previousValue;

    std::optional<std::function<void(T&)>> m_callback;

   public:
    explicit operator bool() const = delete;

    ConfigValue(std::string tag, T* value)
        : m_value(value), m_previousValue(*value) {
        this->m_tag = tag;
    }

    T& inner() { return *m_value; }
    const T& inner() const { return *m_value; }

    ConfigValue& operator=(T value) {
        *m_value = value;
        return *this;
    }

    T operator()() { return *m_value; }

    void handle(std::function<void(T&)> callback) { m_callback = callback; }

    const std::string& getId() override { return this->m_tag; }

    void notifyChange() override {
        if (m_callback.has_value()) {
            m_callback.value()(*m_value);
        }
    }

    void* getValue() override { return m_value; }

    void* getPrevious() override { return &m_previousValue; }

    static ConfigValuePtr<T> create(std::string tag, T* value) {
        auto binding = ConfigValuePtr<T>::make(tag, value);
        BindingManager::get()->registerValue(binding.inner);
        return binding;
    }

    std::shared_ptr<KeybindControl> createKeybind(RawKeybind& kb) override {
        return GrapeKeybind<T>::createFromString(
            kb.m_valueTag, kb.m_key, kb.m_type, kb.m_value, kb.m_modifiers);
    }
};

template <typename T>
struct ConfigValuePtr {
   private:
    using Self = ConfigValuePtr<T>;
    std::shared_ptr<ConfigValue<T>> inner;
    ConfigValuePtr() = default;

   public:
    explicit operator bool() const = delete;

    template <typename... Args>
    static Self make(Args&&... args) {
        Self instance;
        instance.inner =
            std::make_shared<ConfigValue<T>>(std::forward<decltype(args)>(args)...);
        return instance;
    }

    ConfigValue<T>* operator->() { return inner.get(); }
    ConfigValue<T> const* operator->() const { return inner.get(); }

    friend class BindingManager;
    friend class ConfigValue<T>;
};
