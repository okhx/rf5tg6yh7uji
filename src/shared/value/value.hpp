#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <glaze/json/read.hpp>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "keybind.hpp"
#include "util/atomic_file.hpp"

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

    void loadDefaults() {
        struct DefaultKeybind {
            int key;
            int modifiers;
            const char* tag;
        };
        static constexpr std::array defaults = {
            DefaultKeybind{70, 0, "ui.visible"},
            DefaultKeybind{grape::keybinds::FRAME_ADVANCE_KEY, 0,
                           "updater.frame_advance"},
            DefaultKeybind{'B', 0, "updater.advance_back"},
            DefaultKeybind{grape::keybinds::ADVANCE_ONE_KEY, 0,
                           "updater.advance_one"},
            DefaultKeybind{84, 0, "trajectory.enabled"},
            DefaultKeybind{18, 0, "ui.visible"},
        };

        for (const auto& binding : defaults) {
            RawKeybind raw{binding.key, binding.modifiers, binding.tag,
                           KeybindType::Toggle, true, "1"};
            if (!addKeybindForTag(binding.tag, raw)) {
                geode::log::error("Unknown default keybind tag {}",
                                  binding.tag);
            }
        }
    }

    bool m_needsNewKey = false;
    bool m_keyReceived = false;
    cocos2d::enumKeyCodes m_newKey = cocos2d::enumKeyCodes::KEY_None;
    int m_newModifiers = 0;
    KeyPressTracker m_pressedKeys;

    bool m_hasRead = false;

   public:
    static BindingManager* get() {
        static BindingManager instance;
        return &instance;
    }

    void writeToFile(const std::filesystem::path& path) {
        KeybindVector keybinds;
        for (const auto& [_, bucket] : m_keybinds) {
            keybinds.insert(keybinds.end(), bucket.begin(), bucket.end());
        }

        std::string json;
        if (glz::write<glz::opts{.prettify = true}>(keybinds, json)) {
            geode::log::error("Failed to serialize keybinds");
            return;
        }

        std::error_code error;
        if (!grape::files::writeAtomically(path, json, error)) {
            geode::log::error("Failed to save keybinds: {}", error.message());
        }
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

            loadDefaults();
            return;
        }
        geode::log::info("Read keybinds successfully!");

        for (auto& kb : vec) {
            grape::keybinds::migrateLegacy(kb);

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
        m_newModifiers = 0;
    }

    void setNewKey(cocos2d::enumKeyCodes key, bool ctrl, bool shift,
                   bool alt) {
        m_newKey = key;
        m_newModifiers = grape::keybinds::normalizeModifiers(
            static_cast<int>(key),
            grape::keybinds::modifierMask(ctrl, shift, alt));
        m_keyReceived = true;
        m_needsNewKey = false;
    }

    void cancelNewKey() {
        m_needsNewKey = false;
        m_keyReceived = false;
        m_newKey = cocos2d::enumKeyCodes::KEY_None;
        m_newModifiers = 0;
    }

    cocos2d::enumKeyCodes getNewKey() {
        if (m_keyReceived) {
            m_keyReceived = false;
            return m_newKey;
        }
        return cocos2d::enumKeyCodes::KEY_None;
    }

    int getNewModifiers() const { return m_newModifiers; }
    bool isWaitingForKey() const { return m_needsNewKey; }
    bool hasNewKey() const { return m_keyReceived; }

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
        if (const auto value = m_values.find(kb->getTag());
            value != m_values.end() &&
            kb->applyValue(false, value->second->getValue(),
                           value->second->getPrevious())) {
            value->second->notifyChange();
        }
        auto& vec = m_keybinds[hash];
        vec.erase(std::remove(vec.begin(), vec.end(), kb), vec.end());
        if (vec.empty()) m_keybinds.erase(hash);
    }

    void removeAllKeybindsForTag(const std::string& tag) {
        const auto value = m_values.find(tag);
        for (auto& [hash, vec] : m_keybinds) {
            for (const auto& kb : vec) {
                if (kb->getTag() == tag && value != m_values.end() &&
                    kb->applyValue(false, value->second->getValue(),
                                   value->second->getPrevious())) {
                    value->second->notifyChange();
                }
            }
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

    std::shared_ptr<BindingInterface> getValue(const std::string& tag) const {
        const auto found = m_values.find(tag);
        return found == m_values.end() ? nullptr : found->second;
    }

    bool addKeybindForTag(const std::string& tag, RawKeybind& raw) {
        auto value = m_values.find(tag);
        if (value == m_values.end() || !value->second) return false;
        auto kb = value->second->createKeybind(raw);
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
        const int modifiers = grape::keybinds::normalizeModifiers(
            key, grape::keybinds::modifierMask(ctrl, shift, alt));
        KeybindControl::HashT hash = key | (modifiers << 20);

        if (!m_pressedKeys.resolve(key, pressed, hash)) return;

        const auto bucket = m_keybinds.find(hash);
        if (bucket == m_keybinds.end()) return;

        for (const auto& keybind : bucket->second) {
            const auto value = m_values.find(keybind->getTag());
            if (value == m_values.end()) continue;

            if (keybind->applyValue(pressed, value->second->getValue(),
                                    value->second->getPrevious())) {
                value->second->notifyChange();
            }
        }
    }
};

template <typename T>
struct ConfigValuePtr;

template <typename T>
class ConfigValue : public BindingInterface {
   private:
    std::string m_tag;
    T* m_value;
    T m_previousValue;

    std::optional<std::function<void(T&)>> m_callback;

   public:
    explicit operator bool() const = delete;

    ConfigValue(std::string tag, T* value)
        : m_tag(std::move(tag)), m_value(value), m_previousValue(*value) {}

    T& inner() { return *m_value; }
    const T& inner() const { return *m_value; }

    ConfigValue& operator=(T value) {
        *m_value = value;
        return *this;
    }

    T operator()() { return *m_value; }

    void handle(std::function<void(T&)> callback) { m_callback = callback; }

    const std::string& getId() override { return m_tag; }

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
            kb.m_valueTag, kb.m_key, kb.m_type, kb.m_value, kb.m_modifiers,
            kb.m_active);
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
