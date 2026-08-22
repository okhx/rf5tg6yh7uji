#include "script_engine.hpp"

#include <Geode/Geode.hpp>
#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glaze/glaze.hpp>
#include <limits>
#include <unordered_map>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "skeet_menu.hpp"
#include "skrt_ui.hpp"
#include "trajectory/trajectory.hpp"
#include "ui/lib/stb_image.h"
#include "ui/manager.hpp"
#include "util/storage.hpp"

using namespace geode::prelude;

namespace grape::pc {
struct ScriptEngine::Impl {
    struct Memory {
        size_t used = 0;
        static constexpr size_t limit = 16 * 1024 * 1024;
    };

    struct Script {
        Impl* owner;
        std::filesystem::path path;
        std::string status;
        Memory memory;
        lua_State* state = nullptr;
        bool loaded = false;
        bool drawing = false;
        bool overlay = false;
        bool settingsDirty = false;
        std::chrono::steady_clock::time_point settingsChanged{};
        std::unordered_map<std::string, std::string> settings;

        ~Script() {
            if (state) lua_close(state);
        }
    };

    struct Image {
        unsigned texture = 0;
        int width = 0;
        int height = 0;
        int frame = 0;
        int frameCount = 1;
        double nextFrame = 0;
        std::vector<unsigned char> pixels;
        std::vector<int> delays;
    };

    std::vector<std::unique_ptr<Script>> scripts;
    std::unordered_map<std::string, Image> images;
    bool dispatchingInput = false;

    static std::filesystem::path settingsPath(const Script& script) {
        std::string name = script.path.stem().string();
        for (char& character : name)
            if (!std::isalnum(static_cast<unsigned char>(character)) &&
                character != '-' && character != '_')
                character = '_';
        return grape::paths::directory("scripts") / "settings" /
               (name.substr(0, 80) + ".json");
    }

    static void readSettings(Script& script) {
        script.settings.clear();
        const auto path = settingsPath(script);
        if (!std::filesystem::exists(path)) return;
        const auto error = glz::read_file_json(
            script.settings, path.string(), std::string{});
        if (error)
            geode::log::warn("Could not read Lua settings for {}",
                             script.path.filename().string());
    }

    static void writeSettings(Script& script) {
        if (!script.settingsDirty) return;
        std::filesystem::create_directories(settingsPath(script).parent_path());
        const auto error = glz::write_file_json<glz::opts{.prettify = true}>(
            script.settings, settingsPath(script).string(), std::string{});
        if (!error) script.settingsDirty = false;
    }

    static void* allocate(void* data, void* pointer, size_t oldSize,
                          size_t newSize) {
        auto& memory = *static_cast<Memory*>(data);
        const size_t accountedOld = pointer ? oldSize : 0;
        if (!newSize) {
            memory.used -= std::min(memory.used, accountedOld);
            std::free(pointer);
            return nullptr;
        }
        if (newSize > accountedOld &&
            memory.used + newSize - accountedOld > Memory::limit)
            return nullptr;
        void* result = std::realloc(pointer, newSize);
        if (result)
            memory.used = memory.used - std::min(memory.used, accountedOld) + newSize;
        return result;
    }

    static Script& script(lua_State* state) {
        return **static_cast<Script**>(lua_getextraspace(state));
    }

    static PlayerObject* player(lua_State* state, int argument = 1) {
        auto* layer = GJBaseGameLayer::get();
        if (!layer) luaL_error(state, "no level is active");
        const int index = static_cast<int>(luaL_checkinteger(state, argument));
        if (index == 1) return layer->m_player1;
        if (index == 2 && layer->m_player2) return layer->m_player2;
        luaL_error(state, "player must be 1 or 2");
        return nullptr;
    }

    static int log(lua_State* state) {
        geode::log::info("[Lua:{}] {}", script(state).path.filename().string(),
                         luaL_checkstring(state, 1));
        return 0;
    }

    static int notify(lua_State* state) {
        Notification::create(luaL_checkstring(state, 1))->show();
        return 0;
    }

    static int frame(lua_State* state) {
        lua_pushinteger(state, static_cast<lua_Integer>(
            GrapeEngine::get()->timeline().getDisplayFrame()));
        return 1;
    }

    static int inLevel(lua_State* state) {
        lua_pushboolean(state, GJBaseGameLayer::get() &&
            GrapeEngine::get()->trajectory().unsafeInner());
        return 1;
    }

    static int tps(lua_State* state) {
        auto value = GrapeEngine::get()->timeline().m_tps;
        if (lua_gettop(state)) {
            value->inner() = std::clamp(luaL_checknumber(state, 1), 1.0, 1000000.0);
            value->notifyChange();
        }
        lua_pushnumber(state, value->inner());
        return 1;
    }

    static int speed(lua_State* state) {
        auto value = GrapeEngine::get()->timeline().m_speedhack;
        if (lua_gettop(state)) {
            value->inner() = std::clamp(luaL_checknumber(state, 1), 0.01, 100.0);
            value->notifyChange();
        }
        lua_pushnumber(state, value->inner());
        return 1;
    }

    static int input(lua_State* state) {
        const int index = static_cast<int>(luaL_checkinteger(state, 1));
        const int button = static_cast<int>(luaL_checkinteger(state, 2));
        if (index < 1 || index > 2 || button < 1 || button > 3)
            return luaL_error(state, "invalid player or button");
        auto* layer = GJBaseGameLayer::get();
        if (!layer) return luaL_error(state, "no level is active");
        layer->handleButton(lua_toboolean(state, 3), button, index == 1);
        return 0;
    }

    static int playerGet(lua_State* state) {
        auto* p = player(state);
        std::string_view field(luaL_checkstring(state, 2));
        if (field == "x") lua_pushnumber(state, p->getPositionX());
        else if (field == "y") lua_pushnumber(state, p->getPositionY());
        else if (field == "rotation") lua_pushnumber(state, p->getRotation());
        else if (field == "y_velocity") lua_pushnumber(state, p->m_yVelocity);
        else if (field == "x_velocity") lua_pushnumber(state, p->m_platformerXVelocity);
        else if (field == "gravity") lua_pushnumber(state, p->m_gravity);
        else if (field == "scale") lua_pushnumber(state, p->getScale());
        else if (field == "opacity") lua_pushinteger(state, p->getOpacity());
        else if (field == "visible") lua_pushboolean(state, p->isVisible());
        else if (field == "dead") lua_pushboolean(state, p->m_isDead);
        else if (field == "holding") lua_pushboolean(state,
            p->m_holdingButtons[static_cast<int>(PlayerButton::Jump)]);
        else if (field == "upside_down") lua_pushboolean(state, p->m_isUpsideDown);
        else if (field == "going_left") lua_pushboolean(state, p->m_isGoingLeft);
        else if (field == "cube") lua_pushboolean(state, p->isInNormalMode());
        else if (field == "ship") lua_pushboolean(state, p->m_isShip);
        else if (field == "ufo") lua_pushboolean(state, p->m_isBird);
        else if (field == "ball") lua_pushboolean(state, p->m_isBall);
        else if (field == "wave") lua_pushboolean(state, p->m_isDart);
        else if (field == "robot") lua_pushboolean(state, p->m_isRobot);
        else if (field == "spider") lua_pushboolean(state, p->m_isSpider);
        else if (field == "swing") lua_pushboolean(state, p->m_isSwing);
        else if (field == "platformer") lua_pushboolean(state, p->m_isPlatformer);
        else return luaL_error(state, "unknown player field");
        return 1;
    }

    static int playerSet(lua_State* state) {
        auto* p = player(state);
        std::string_view field(luaL_checkstring(state, 2));
        if (field == "visible") {
            p->setVisible(lua_toboolean(state, 3));
            return 0;
        }
        const double raw = luaL_checknumber(state, 3);
        if (!std::isfinite(raw)) return luaL_error(state, "value must be finite");
        if (field == "x") p->setPositionX(std::clamp(raw, -1e9, 1e9));
        else if (field == "y") p->setPositionY(std::clamp(raw, -1e9, 1e9));
        else if (field == "rotation") p->setRotation(static_cast<float>(raw));
        else if (field == "y_velocity") p->m_yVelocity = std::clamp(raw, -1e6, 1e6);
        else if (field == "x_velocity") p->m_platformerXVelocity = std::clamp(raw, -1e6, 1e6);
        else if (field == "gravity") p->m_gravity = std::clamp(raw, -100.0, 100.0);
        else if (field == "scale") p->setScale(static_cast<float>(std::clamp(raw, 0.05, 10.0)));
        else if (field == "opacity") p->setOpacity(static_cast<unsigned char>(std::clamp(raw, 0.0, 255.0)));
        else return luaL_error(state, "unknown or read-only player field");
        return 0;
    }

    static int trajectory(lua_State* state) {
        auto* layer = GJBaseGameLayer::get();
        auto* simulator = GrapeEngine::get()->trajectory().unsafeInner();
        if (!layer || !simulator) return luaL_error(state, "no level is active");

        const int index = static_cast<int>(luaL_checkinteger(state, 1));
        if (index < 1 || index > 2 || (index == 2 && !layer->m_player2))
            return luaL_error(state, "player must be 1 or 2");
        const std::string_view action(luaL_checkstring(state, 2));
        const int delay = static_cast<int>(luaL_checkinteger(state, 3));
        const int frames = static_cast<int>(luaL_checkinteger(state, 4));
        if (delay < 0 || frames < 1 || frames > 600 || delay >= frames)
            return luaL_error(state, "require 0 <= delay < frames <= 600");

        using Mode = Trajectory::TrajectoryMode;
        int mode = index == 1 ? Mode::Player1 : Mode::Player2;
        if (action == "click") mode |= Mode::Release;
        else if (action == "release") mode |= Mode::Hold;
        else return luaL_error(state, "action must be click or release");

        const auto result = simulator->simulate(
            layer, index == 1, mode, false,
            {.m_bypassConfig = true,
             .m_maxLength = frames,
             .m_draw = false,
             .m_flipAtStep = delay});
        lua_createtable(state, 0, 7);
        const auto number = [&](const char* key, double value) {
            lua_pushnumber(state, value);
            lua_setfield(state, -2, key);
        };
        number("x", result.position.x);
        number("y", result.position.y);
        number("frames", result.score);
        number("min_y", result.minY);
        number("max_y", result.maxY);
        lua_pushboolean(state, !result.dead);
        lua_setfield(state, -2, "survived");
        return 1;
    }

    static std::shared_ptr<BindingInterface> menuValue(lua_State* state) {
        const std::string tag(luaL_checkstring(state, 1));
        auto value = BindingManager::get()->getValue(tag);
        if (!value) luaL_error(state, "unknown menu control: %s", tag.c_str());
        return value;
    }

    static int menuList(lua_State* state) {
        const auto tags = BindingManager::get()->getValueTags();
        lua_createtable(state, static_cast<int>(tags.size()), 0);
        int index = 1;
        for (const auto& tag : tags) {
            lua_pushlstring(state, tag.data(), tag.size());
            lua_rawseti(state, -2, index++);
        }
        return 1;
    }

    static int menuGet(lua_State* state) {
        auto value = menuValue(state);
        if (auto* item = dynamic_cast<ConfigValue<bool>*>(value.get()))
            lua_pushboolean(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<int>*>(value.get()))
            lua_pushinteger(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<uint32_t>*>(value.get()))
            lua_pushinteger(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<float>*>(value.get()))
            lua_pushnumber(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<double>*>(value.get()))
            lua_pushnumber(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<std::string>*>(value.get()))
            lua_pushlstring(state, item->inner().data(), item->inner().size());
        else
            return luaL_error(state, "unsupported menu control type");
        return 1;
    }

    static int menuSet(lua_State* state) {
        auto value = menuValue(state);
        if (auto* item = dynamic_cast<ConfigValue<bool>*>(value.get()))
            item->inner() = lua_toboolean(state, 2);
        else if (auto* item = dynamic_cast<ConfigValue<int>*>(value.get())) {
            const auto number = luaL_checkinteger(state, 2);
            if (number < std::numeric_limits<int>::min() ||
                number > std::numeric_limits<int>::max())
                return luaL_error(state, "value is outside int range");
            item->inner() = static_cast<int>(number);
        }
        else if (auto* item = dynamic_cast<ConfigValue<uint32_t>*>(value.get())) {
            const auto number = luaL_checkinteger(state, 2);
            if (number < 0 || static_cast<uint64_t>(number) > UINT32_MAX)
                return luaL_error(state, "value is outside uint32 range");
            item->inner() = static_cast<uint32_t>(number);
        } else if (auto* item = dynamic_cast<ConfigValue<float>*>(value.get())) {
            const double number = luaL_checknumber(state, 2);
            if (!std::isfinite(number)) return luaL_error(state, "value must be finite");
            item->inner() = static_cast<float>(number);
        } else if (auto* item = dynamic_cast<ConfigValue<double>*>(value.get())) {
            const double number = luaL_checknumber(state, 2);
            if (!std::isfinite(number)) return luaL_error(state, "value must be finite");
            item->inner() = number;
        } else if (auto* item = dynamic_cast<ConfigValue<std::string>*>(value.get())) {
            size_t size = 0;
            const char* text = luaL_checklstring(state, 2, &size);
            if (size > 4096) return luaL_error(state, "value is too long");
            item->inner().assign(text, size);
        } else
            return luaL_error(state, "unsupported menu control type");
        value->notifyChange();
        return 0;
    }

    static int menuToggle(lua_State* state) {
        auto value = menuValue(state);
        auto* item = dynamic_cast<ConfigValue<bool>*>(value.get());
        if (!item) return luaL_error(state, "menu.toggle requires a boolean control");
        item->inner() = !item->inner();
        item->notifyChange();
        lua_pushboolean(state, item->inner());
        return 1;
    }

    static int menuPress(lua_State* state) {
        auto value = menuValue(state);
        auto* item = dynamic_cast<ConfigValue<bool>*>(value.get());
        if (!item) return luaL_error(state, "menu.press requires a boolean control");
        item->inner() = true;
        item->notifyChange();
        return 0;
    }

    static int menuMode(lua_State* state) {
        auto* engine = GrapeEngine::get();
        if (lua_gettop(state)) {
            const std::string_view mode(luaL_checkstring(state, 1));
            if (mode == "stopped") engine->setMode(GrapeEngine::Stopped);
            else if (mode == "recording") engine->setMode(GrapeEngine::Recording);
            else if (mode == "playing") engine->setMode(GrapeEngine::Playing);
            else return luaL_error(state, "mode must be stopped, recording or playing");
        }
        lua_pushstring(state, engine->isRecording() ? "recording" :
                              engine->isPlaying() ? "playing" : "stopped");
        return 1;
    }

    static int storageGet(lua_State* state) {
        auto& owner = script(state);
        const std::string key(luaL_checkstring(state, 1));
        if (key.empty() || key.size() > 64)
            return luaL_error(state, "setting key must contain 1-64 characters");
        const auto found = owner.settings.find(key);
        if (found == owner.settings.end()) {
            luaL_checkany(state, 2);
            lua_pushvalue(state, 2);
            return 1;
        }
        const std::string_view value(found->second);
        if (value == "b:1" || value == "b:0")
            lua_pushboolean(state, value.back() == '1');
        else if (value.starts_with("n:")) {
            double number = 0;
            const auto parsed = std::from_chars(
                value.data() + 2, value.data() + value.size(), number);
            if (parsed.ec != std::errc{}) {
                lua_pushvalue(state, 2);
                return 1;
            }
            lua_pushnumber(state, number);
        } else if (value.starts_with("s:"))
            lua_pushlstring(state, value.data() + 2, value.size() - 2);
        else
            lua_pushvalue(state, 2);
        return 1;
    }

    static int storageSet(lua_State* state) {
        auto& owner = script(state);
        const std::string key(luaL_checkstring(state, 1));
        if (key.empty() || key.size() > 64)
            return luaL_error(state, "setting key must contain 1-64 characters");
        std::string encoded;
        switch (lua_type(state, 2)) {
            case LUA_TBOOLEAN:
                encoded = lua_toboolean(state, 2) ? "b:1" : "b:0";
                break;
            case LUA_TNUMBER: {
                const double number = lua_tonumber(state, 2);
                if (!std::isfinite(number))
                    return luaL_error(state, "setting number must be finite");
                char buffer[48]{};
                const auto result = std::to_chars(
                    buffer, buffer + sizeof(buffer), number,
                    std::chars_format::general, 17);
                encoded = "n:" + std::string(buffer, result.ptr);
                break;
            }
            case LUA_TSTRING: {
                size_t length = 0;
                const char* value = lua_tolstring(state, 2, &length);
                if (length > 4096)
                    return luaL_error(state, "setting string is too long");
                encoded = "s:" + std::string(value, length);
                break;
            }
            default:
                return luaL_error(state, "settings support booleans, numbers and strings");
        }
        owner.settings[key] = std::move(encoded);
        owner.settingsDirty = true;
        owner.settingsChanged = std::chrono::steady_clock::now();
        return 0;
    }

    static void requireDraw(lua_State* state) {
        auto& owner = script(state);
        if (!owner.drawing || owner.overlay)
            luaL_error(state, "this ui function requires on_draw");
    }

    static int uiText(lua_State* state) {
        requireDraw(state);
        ImGui::TextWrapped("%s", luaL_checkstring(state, 1));
        return 0;
    }

    static int uiButton(lua_State* state) {
        requireDraw(state);
        lua_pushboolean(state, ImGui::Button(luaL_checkstring(state, 1)));
        return 1;
    }

    static int uiCheckbox(lua_State* state) {
        requireDraw(state);
        bool value = lua_toboolean(state, 2);
        ImGui::Checkbox(luaL_checkstring(state, 1), &value);
        lua_pushboolean(state, value);
        return 1;
    }

    static bool filledSlider(const char* label, double& value, double minimum,
                             double maximum) {
        const float width = ImGui::CalcItemWidth();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + ImGui::GetFrameHeight());
        ImGui::InvisibleButton(label, ImVec2(width, ImGui::GetFrameHeight()));
        const double before = value;
        if (maximum > minimum && ImGui::IsItemActive() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const double ratio = std::clamp(
                (ImGui::GetIO().MousePos.x - min.x) / width, 0.0f, 1.0f);
            value = minimum + (maximum - minimum) * ratio;
        }
        const float ratio = maximum > minimum
            ? static_cast<float>(std::clamp(
                  (value - minimum) / (maximum - minimum), 0.0, 1.0))
            : 0.0f;
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, IM_COL32(24, 21, 24, 255), 4.0f);
        draw->AddRect(min, max, IM_COL32(34, 34, 34, 255), 4.0f);
        if (ratio > 0.0f) {
            const auto& accent = GrapeSettings::get()->grapeAccent;
            draw->AddRectFilled(
                ImVec2(min.x + 2.0f, min.y + 2.0f),
                ImVec2(min.x + 2.0f + (width - 4.0f) * ratio, max.y - 2.0f),
                ImGui::ColorConvertFloat4ToU32(
                    {accent[0], accent[1], accent[2], accent[3]}),
                3.0f);
        }
        char text[64]{};
        std::snprintf(text, sizeof(text), "%.3f", value);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 textPos(min.x + (width - textSize.x) * .5f,
                             min.y + (max.y - min.y - textSize.y) * .5f);
        draw->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
                      IM_COL32(0, 0, 0, 190), text);
        draw->AddText(textPos, IM_COL32_WHITE, text);
        const char* marker = std::strstr(label, "##");
        const char* visibleEnd = marker ? marker : label + std::strlen(label);
        if (visibleEnd != label) {
            draw->AddText(
                ImVec2(max.x + ImGui::GetStyle().ItemInnerSpacing.x,
                       min.y + (max.y - min.y - ImGui::GetFontSize()) * .5f),
                ImGui::GetColorU32(ImGuiCol_Text), label, visibleEnd);
        }
        return value != before;
    }

    static int uiSlider(lua_State* state) {
        requireDraw(state);
        double value = luaL_checknumber(state, 2);
        const double minimum = luaL_checknumber(state, 3);
        const double maximum = luaL_checknumber(state, 4);
        if (grape::pc::useSkeetMenu())
            grape::pc::skrt::sliderScalar(
                luaL_checkstring(state, 1), ImGuiDataType_Double, &value,
                &minimum, &maximum);
        else if (slui::Config::get().customMode)
            filledSlider(luaL_checkstring(state, 1), value, minimum, maximum);
        else
            ImGui::SliderScalar(luaL_checkstring(state, 1), ImGuiDataType_Double,
                                &value, &minimum, &maximum, "%.3f",
                                ImGuiSliderFlags_AlwaysClamp);
        lua_pushnumber(state, value);
        return 1;
    }

    static int uiInput(lua_State* state) {
        requireDraw(state);
        const char* label = luaL_checkstring(state, 1);
        double value = luaL_checknumber(state, 2);
        if (grape::pc::useSkeetMenu()) {
            const double minimum = -std::numeric_limits<double>::max();
            const double maximum = std::numeric_limits<double>::max();
            grape::pc::skrt::dragScalar(
                label, ImGuiDataType_Double, &value, &minimum, &maximum, 0.0f);
        } else {
            ImGui::InputDouble(label, &value, 0.0, 0.0, "%.3f",
                               ImGuiInputTextFlags_AutoSelectAll);
        }
        lua_pushnumber(state, value);
        return 1;
    }

    static int uiSameLine(lua_State* state) {
        requireDraw(state);
        ImGui::SameLine();
        return 0;
    }

    static int uiSeparator(lua_State* state) {
        requireDraw(state);
        ImGui::Separator();
        return 0;
    }

    static void upload(Image& image) {
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        if (!image.texture) glGenTextures(1, &image.texture);
        glBindTexture(GL_TEXTURE_2D, image.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const auto* frame = image.pixels.data() +
            static_cast<size_t>(image.frame) * image.width * image.height * 4;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, frame);
        glBindTexture(GL_TEXTURE_2D, previous);
    }

    Image* image(Script& owner, const std::string& name) {
        const std::filesystem::path relative(name);
        if (relative.is_absolute()) return nullptr;
        for (const auto& part : relative)
            if (part == "..") return nullptr;
        const auto path = owner.path.parent_path() / relative;
        const auto key = path.lexically_normal().string();
        if (auto found = images.find(key); found != images.end()) return &found->second;
        std::ifstream file(path, std::ios::binary);
        if (!file) return nullptr;
        std::vector<unsigned char> bytes(
            std::istreambuf_iterator<char>(file), {});
        Image result;
        int channels = 0;
        unsigned char* decoded = nullptr;
        if (path.extension() == ".gif") {
            int* delays = nullptr;
            decoded = stbi_load_gif_from_memory(
                bytes.data(), static_cast<int>(bytes.size()), &delays,
                &result.width, &result.height, &result.frameCount, &channels, 4);
            if (delays) {
                result.delays.assign(delays, delays + result.frameCount);
                stbi_image_free(delays);
            }
        } else {
            decoded = stbi_load_from_memory(
                bytes.data(), static_cast<int>(bytes.size()), &result.width,
                &result.height, &channels, 4);
        }
        if (!decoded || result.width <= 0 || result.height <= 0 ||
            result.frameCount <= 0) {
            stbi_image_free(decoded);
            return nullptr;
        }
        const size_t size = static_cast<size_t>(result.width) * result.height *
                            result.frameCount * 4;
        result.pixels.assign(decoded, decoded + size);
        stbi_image_free(decoded);
        upload(result);
        return &images.emplace(key, std::move(result)).first->second;
    }

    static void animate(Image& image) {
        if (image.frameCount <= 1) return;
        const double now = ImGui::GetTime() * 1000.0;
        if (!image.nextFrame)
            image.nextFrame = now + std::max(10, image.delays.empty() ? 100 : image.delays[0]);
        if (now < image.nextFrame) return;
        image.frame = (image.frame + 1) % image.frameCount;
        image.nextFrame = now + std::max(10, image.delays.empty() ? 100 : image.delays[image.frame]);
        upload(image);
    }

    static int uiImage(lua_State* state) {
        requireDraw(state);
        auto& owner = script(state);
        auto* image = owner.owner->image(owner, luaL_checkstring(state, 1));
        if (!image) return luaL_error(state, "image could not be loaded");
        animate(*image);
        const float width = static_cast<float>(luaL_optnumber(state, 2, image->width));
        const float height = static_cast<float>(luaL_optnumber(state, 3, image->height));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image->texture)),
                     {width, height});
        return 0;
    }

    static int uiCornerImage(lua_State* state) {
        auto& owner = script(state);
        if (!owner.drawing || !owner.overlay)
            return luaL_error(state, "corner_image requires on_overlay");
        auto* image = owner.owner->image(owner, luaL_checkstring(state, 1));
        if (!image) return luaL_error(state, "image could not be loaded");
        const std::string_view corner(luaL_checkstring(state, 2));
        const float width = static_cast<float>(luaL_optnumber(state, 3, image->width));
        const float height = static_cast<float>(luaL_optnumber(state, 4, image->height));
        const float padding = static_cast<float>(luaL_optnumber(state, 5, 12.0));
        if (!std::isfinite(width) || !std::isfinite(height) ||
            !std::isfinite(padding) || width <= 0 || height <= 0 ||
            width > 4096 || height > 4096 || padding < 0 || padding > 1024)
            return luaL_error(state, "invalid image size or padding");

        const auto* viewport = ImGui::GetMainViewport();
        ImVec2 position(viewport->Pos.x + padding, viewport->Pos.y + padding);
        if (corner == "top_right" || corner == "bottom_right")
            position.x = viewport->Pos.x + viewport->Size.x - width - padding;
        if (corner == "bottom_left" || corner == "bottom_right")
            position.y = viewport->Pos.y + viewport->Size.y - height - padding;
        if (corner != "top_left" && corner != "top_right" &&
            corner != "bottom_left" && corner != "bottom_right")
            return luaL_error(state, "invalid corner");

        animate(*image);
        ImGui::GetForegroundDrawList()->AddImage(
            ImTextureRef(static_cast<ImTextureID>(image->texture)), position,
            ImVec2(position.x + width, position.y + height));
        return 0;
    }

    static GJBaseGameLayer* requireLevelDraw(lua_State* state) {
        auto& owner = script(state);
        if (!owner.drawing || !owner.overlay)
            luaL_error(state, "level drawing requires on_overlay");
        auto* layer = GJBaseGameLayer::get();
        if (!layer || !layer->m_objectLayer)
            luaL_error(state, "no level is active");
        return layer;
    }

    static ImVec2 worldToScreen(GJBaseGameLayer* layer, float x, float y) {
        const auto world = layer->m_objectLayer->convertToWorldSpace({x, y});
        const auto size = CCDirector::get()->getWinSize();
        const auto* viewport = ImGui::GetMainViewport();
        return {
            viewport->Pos.x + world.x / size.width * viewport->Size.x,
            viewport->Pos.y + (1.0f - world.y / size.height) * viewport->Size.y,
        };
    }

    static float worldSize(GJBaseGameLayer* layer, float x, float y,
                           float size) {
        const auto a = worldToScreen(layer, x, y);
        const auto b = worldToScreen(layer, x + size, y);
        return std::hypot(b.x - a.x, b.y - a.y);
    }

    static float levelNumber(lua_State* state, int argument) {
        const float value = static_cast<float>(luaL_checknumber(state, argument));
        if (!std::isfinite(value)) luaL_error(state, "coordinate must be finite");
        return value;
    }

    static float levelWidth(lua_State* state, int argument) {
        const float value = static_cast<float>(luaL_optnumber(state, argument, 1.0));
        if (!std::isfinite(value) || value <= 0 || value > 10000)
            luaL_error(state, "width must be between 0 and 10000");
        return value;
    }

    static ImU32 levelColor(lua_State* state, int argument) {
        float color[4] = {
            static_cast<float>(luaL_checknumber(state, argument)),
            static_cast<float>(luaL_checknumber(state, argument + 1)),
            static_cast<float>(luaL_checknumber(state, argument + 2)),
            static_cast<float>(luaL_optnumber(state, argument + 3, 1.0)),
        };
        for (float& channel : color) {
            if (!std::isfinite(channel))
                luaL_error(state, "color must be finite");
            channel = std::clamp(channel, 0.0f, 1.0f);
        }
        return ImGui::ColorConvertFloat4ToU32(
            {color[0], color[1], color[2], color[3]});
    }

    static std::vector<ImVec2> levelPoints(lua_State* state,
                                           GJBaseGameLayer* layer) {
        luaL_checktype(state, 1, LUA_TTABLE);
        const size_t count = lua_rawlen(state, 1);
        if (count < 4 || count > 8192 || count % 2)
            luaL_error(state, "points must contain 2-4096 x,y pairs");
        std::vector<ImVec2> points;
        points.reserve(count / 2);
        for (size_t i = 1; i <= count; i += 2) {
            lua_rawgeti(state, 1, i);
            lua_rawgeti(state, 1, i + 1);
            const float x = static_cast<float>(luaL_checknumber(state, -2));
            const float y = static_cast<float>(luaL_checknumber(state, -1));
            lua_pop(state, 2);
            if (!std::isfinite(x) || !std::isfinite(y))
                luaL_error(state, "points must be finite");
            points.push_back(worldToScreen(layer, x, y));
        }
        return points;
    }

    static int levelWorldToScreen(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const auto point = worldToScreen(
            layer, levelNumber(state, 1), levelNumber(state, 2));
        lua_pushnumber(state, point.x);
        lua_pushnumber(state, point.y);
        const auto* viewport = ImGui::GetMainViewport();
        lua_pushboolean(state, point.x >= viewport->Pos.x &&
            point.y >= viewport->Pos.y &&
            point.x <= viewport->Pos.x + viewport->Size.x &&
            point.y <= viewport->Pos.y + viewport->Size.y);
        return 3;
    }

    static int levelLine(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const float x1 = levelNumber(state, 1);
        const float y1 = levelNumber(state, 2);
        const float x2 = levelNumber(state, 3);
        const float y2 = levelNumber(state, 4);
        const float width = levelWidth(state, 9);
        ImGui::GetBackgroundDrawList()->AddLine(
            worldToScreen(layer, x1, y1), worldToScreen(layer, x2, y2),
            levelColor(state, 5), std::max(0.5f, worldSize(layer, x1, y1, width)));
        return 0;
    }

    static int levelPolyline(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const auto points = levelPoints(state, layer);
        const float width = levelWidth(state, 7);
        ImGui::GetBackgroundDrawList()->AddPolyline(
            points.data(), static_cast<int>(points.size()), levelColor(state, 2),
            lua_toboolean(state, 6) ? ImDrawFlags_Closed : 0,
            std::max(0.5f, worldSize(layer, 0, 0, width)));
        return 0;
    }

    static int levelPolygon(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const auto points = levelPoints(state, layer);
        if (points.size() < 3) return luaL_error(state, "polygon needs 3 points");
        auto* draw = ImGui::GetBackgroundDrawList();
        const ImU32 color = levelColor(state, 2);
        if (lua_isnoneornil(state, 6) || lua_toboolean(state, 6))
            draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
        else
            draw->AddPolyline(
                points.data(), static_cast<int>(points.size()), color,
                ImDrawFlags_Closed,
                std::max(0.5f, worldSize(layer, 0, 0,
                    levelWidth(state, 7))));
        return 0;
    }

    static int levelRect(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const float x1 = levelNumber(state, 1);
        const float y1 = levelNumber(state, 2);
        const float x2 = levelNumber(state, 3);
        const float y2 = levelNumber(state, 4);
        const ImVec2 points[4] = {
            worldToScreen(layer, x1, y1), worldToScreen(layer, x2, y1),
            worldToScreen(layer, x2, y2), worldToScreen(layer, x1, y2),
        };
        auto* draw = ImGui::GetBackgroundDrawList();
        const ImU32 color = levelColor(state, 5);
        if (lua_isnoneornil(state, 9) || lua_toboolean(state, 9))
            draw->AddQuadFilled(points[0], points[1], points[2], points[3], color);
        else
            draw->AddPolyline(points, 4, color, ImDrawFlags_Closed,
                std::max(0.5f, worldSize(layer, x1, y1,
                    levelWidth(state, 10))));
        return 0;
    }

    static int levelCircle(lua_State* state) {
        auto* layer = requireLevelDraw(state);

        const float x = levelNumber(state, 1);
        const float y = levelNumber(state, 2);
        const float radius = levelNumber(state, 3);
        if (radius <= 0 || radius > 10000)
            return luaL_error(state, "invalid circle position or radius");
        const auto center = worldToScreen(layer, x, y);
        const float screenRadius = worldSize(layer, x, y, radius);
        const ImU32 color = levelColor(state, 4);
        const int segments = std::clamp(
            static_cast<int>(luaL_optinteger(state, 10, 0)), 0, 256);
        auto* draw = ImGui::GetBackgroundDrawList();
        if (lua_isnoneornil(state, 8) || lua_toboolean(state, 8))
            draw->AddCircleFilled(center, screenRadius, color, segments);
        else
            draw->AddCircle(center, screenRadius, color, segments,
                std::max(0.5f, worldSize(layer, x, y,
                    levelWidth(state, 9))));
        return 0;
    }

    static int levelText(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const float x = levelNumber(state, 1);
        const float y = levelNumber(state, 2);
        const char* text = luaL_checkstring(state, 3);
        const float size = worldSize(layer, x, y, levelWidth(state, 4));
        ImVec2 position = worldToScreen(layer, x, y);
        if (lua_toboolean(state, 9)) {
            const auto measured = ImGui::GetFont()->CalcTextSizeA(
                size, std::numeric_limits<float>::max(), 0.0f, text);
            position.x -= measured.x * 0.5f;
            position.y -= measured.y * 0.5f;
        }
        ImGui::GetBackgroundDrawList()->AddText(
            ImGui::GetFont(), size, position, levelColor(state, 5), text);
        return 0;
    }

    static int levelImage(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        auto& owner = script(state);
        auto* image = owner.owner->image(owner, luaL_checkstring(state, 1));
        if (!image) return luaL_error(state, "image could not be loaded");
        const float x = levelNumber(state, 2);
        const float y = levelNumber(state, 3);
        const float width = static_cast<float>(luaL_optnumber(state, 4, image->width));
        const float height = static_cast<float>(luaL_optnumber(state, 5, image->height));
        const float rawAlpha = static_cast<float>(luaL_optnumber(state, 6, 1.0));
        if (!std::isfinite(rawAlpha)) return luaL_error(state, "alpha must be finite");
        const float alpha = std::clamp(rawAlpha, 0.0f, 1.0f);
        if (!std::isfinite(x) || !std::isfinite(y) ||
            !std::isfinite(width) || !std::isfinite(height) ||
            width <= 0 || height <= 0 || width > 10000 || height > 10000)
            return luaL_error(state, "invalid image position or size");
        animate(*image);
        ImGui::GetBackgroundDrawList()->AddImageQuad(
            ImTextureRef(static_cast<ImTextureID>(image->texture)),
            worldToScreen(layer, x, y + height),
            worldToScreen(layer, x + width, y + height),
            worldToScreen(layer, x + width, y),
            worldToScreen(layer, x, y), {0, 0}, {1, 0}, {1, 1}, {0, 1},
            IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)));
        return 0;
    }

    static void addFunction(lua_State* state, const char* name,
                            lua_CFunction function) {
        lua_pushcfunction(state, function);
        lua_setfield(state, -2, name);
    }

    static void instructionLimit(lua_State* state, lua_Debug*) {
        luaL_error(state, "instruction limit exceeded");
    }

    void fail(Script& script, const char* fallback) {
        const char* error = lua_tostring(script.state, -1);
        script.status = error ? error : fallback;
        lua_pop(script.state, 1);
        script.loaded = false;
        geode::log::error("Lua {}: {}", script.path.filename().string(),
                          script.status);
    }

    bool run(Script& script, int arguments) {
        lua_sethook(script.state, instructionLimit, LUA_MASKCOUNT, 250000);
        const int result = lua_pcall(script.state, arguments, 0, 0);
        lua_sethook(script.state, nullptr, 0, 0);
        if (result != LUA_OK) {
            fail(script, "script failed");
            return false;
        }
        return true;
    }

    bool callback(Script& script, const char* name,
                  double argument = 0, bool hasArgument = false) {
        lua_getglobal(script.state, name);
        if (!lua_isfunction(script.state, -1)) {
            lua_pop(script.state, 1);
            return true;
        }
        if (hasArgument) lua_pushnumber(script.state, argument);
        return run(script, hasArgument ? 1 : 0);
    }

    bool inputCallback(Script& script, int player, int button, bool pressed) {
        lua_getglobal(script.state, "on_input");
        if (!lua_isfunction(script.state, -1)) {
            lua_pop(script.state, 1);
            return true;
        }
        lua_pushinteger(script.state, player);
        lua_pushinteger(script.state, button);
        lua_pushboolean(script.state, pressed);
        return run(script, 3);
    }

    void createApi(Script& script) {
        auto* state = script.state;
        lua_newtable(state);
        addFunction(state, "log", log);
        addFunction(state, "notify", notify);
        addFunction(state, "frame", frame);
        addFunction(state, "in_level", inLevel);
        addFunction(state, "tps", tps);
        addFunction(state, "speed", speed);
        addFunction(state, "input", input);
        addFunction(state, "player_get", playerGet);
        addFunction(state, "player_set", playerSet);
        addFunction(state, "trajectory", trajectory);
        lua_newtable(state);
        addFunction(state, "list", menuList);
        addFunction(state, "get", menuGet);
        addFunction(state, "set", menuSet);
        addFunction(state, "toggle", menuToggle);
        addFunction(state, "press", menuPress);
        addFunction(state, "mode", menuMode);
        lua_setfield(state, -2, "menu");
        lua_newtable(state);
        addFunction(state, "get", storageGet);
        addFunction(state, "set", storageSet);
        lua_setfield(state, -2, "storage");
        lua_newtable(state);
        addFunction(state, "text", uiText);
        addFunction(state, "button", uiButton);
        addFunction(state, "checkbox", uiCheckbox);
        addFunction(state, "slider", uiSlider);
        addFunction(state, "input", uiInput);
        addFunction(state, "same_line", uiSameLine);
        addFunction(state, "separator", uiSeparator);
        addFunction(state, "image", uiImage);
        addFunction(state, "corner_image", uiCornerImage);
        lua_setfield(state, -2, "ui");
        lua_newtable(state);
        addFunction(state, "world_to_screen", levelWorldToScreen);
        addFunction(state, "line", levelLine);
        addFunction(state, "polyline", levelPolyline);
        addFunction(state, "polygon", levelPolygon);
        addFunction(state, "rect", levelRect);
        addFunction(state, "circle", levelCircle);
        addFunction(state, "text", levelText);
        addFunction(state, "image", levelImage);
        lua_setfield(state, -2, "level");
        lua_setglobal(state, "grape");
    }

    bool load(Script& script) {
        writeSettings(script);
        if (script.state) lua_close(script.state);
        script.memory = {};
        readSettings(script);
        script.state = lua_newstate(allocate, &script.memory);
        if (!script.state) {
            script.status = "could not create Lua state";
            return false;
        }
        *static_cast<Script**>(lua_getextraspace(script.state)) = &script;
        luaL_requiref(script.state, "_G", luaopen_base, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(script.state, 1);
        for (const char* blocked : {"dofile", "loadfile"}) {
            lua_pushnil(script.state);
            lua_setglobal(script.state, blocked);
        }
        createApi(script);
        if (luaL_loadfile(script.state, script.path.string().c_str()) != LUA_OK) {
            fail(script, "could not compile script");
            return false;
        }
        script.loaded = true;
        script.status = "Loaded";
        if (!run(script, 0) || !callback(script, "on_load")) return false;
        return true;
    }
};

ScriptEngine& ScriptEngine::get() {
    static ScriptEngine engine;
    return engine;
}

ScriptEngine::ScriptEngine() : m_impl(std::make_unique<Impl>()) {}
ScriptEngine::~ScriptEngine() {
    for (auto& script : m_impl->scripts) m_impl->writeSettings(*script);
}

void ScriptEngine::refresh() {
    const auto directory = grape::paths::directory("scripts");
    std::filesystem::create_directories(directory);
    std::filesystem::create_directories(directory / "resources");
    std::filesystem::create_directories(directory / "settings");
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;
        const auto found = std::find_if(m_impl->scripts.begin(), m_impl->scripts.end(),
            [&](const auto& script) { return script->path == entry.path(); });
        if (found == m_impl->scripts.end()) {
            auto script = std::make_unique<Impl::Script>();
            script->owner = m_impl.get();
            script->path = entry.path();
            script->status = "Not loaded";
            m_impl->scripts.push_back(std::move(script));
        }
    }
    std::erase_if(m_impl->scripts, [](const auto& script) {
        return !script->loaded && !std::filesystem::exists(script->path);
    });
    std::ranges::sort(m_impl->scripts, {}, [](const auto& script) {
        return script->path.filename().string();
    });
}

std::vector<ScriptStatus> ScriptEngine::scripts() const {
    std::vector<ScriptStatus> result;
    for (const auto& script : m_impl->scripts)
        result.push_back({script->path.filename().string(), script->status,
                          script->loaded});
    return result;
}

bool ScriptEngine::load(const std::string& name) {
    const auto found = std::find_if(m_impl->scripts.begin(), m_impl->scripts.end(),
        [&](const auto& script) { return script->path.filename() == name; });
    return found != m_impl->scripts.end() && m_impl->load(**found);
}

void ScriptEngine::unload(const std::string& name) {
    const auto found = std::find_if(m_impl->scripts.begin(), m_impl->scripts.end(),
        [&](const auto& script) { return script->path.filename() == name; });
    if (found == m_impl->scripts.end()) return;
    auto& script = **found;
    if (script.loaded) m_impl->callback(script, "on_unload");
    m_impl->writeSettings(script);
    script.loaded = false;
    script.status = "Not loaded";
    if (script.state) {
        lua_close(script.state);
        script.state = nullptr;
    }
}

void ScriptEngine::update(double dt) {
    const auto now = std::chrono::steady_clock::now();
    for (auto& script : m_impl->scripts) {
        if (script->loaded) m_impl->callback(*script, "on_update", dt, true);
        if (script->settingsDirty &&
            now - script->settingsChanged >= std::chrono::milliseconds(500))
            m_impl->writeSettings(*script);
    }
}

void ScriptEngine::input(int player, int button, bool pressed) {
    if (m_impl->dispatchingInput) return;
    m_impl->dispatchingInput = true;
    for (auto& script : m_impl->scripts)
        if (script->loaded)
            m_impl->inputCallback(*script, player, button, pressed);
    m_impl->dispatchingInput = false;
}

void ScriptEngine::overlay() {
    for (auto& script : m_impl->scripts) {
        if (!script->loaded) continue;
        script->drawing = true;
        script->overlay = true;
        m_impl->callback(*script, "on_overlay");
        script->overlay = false;
        script->drawing = false;
    }
}

void ScriptEngine::draw() {
    for (auto& script : m_impl->scripts) {
        if (!script->loaded) continue;
        script->drawing = true;
        m_impl->callback(*script, "on_draw");
        script->drawing = false;
    }
}
}
