#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include "manager.hpp"

void slui::bringCurrentWindowToFront() {
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
}

#ifdef GEODE_IS_WINDOWS
#include <winuser.h>
#else
constexpr int VK_BACK = 0x08;
constexpr int VK_TAB = 0x09;
constexpr int VK_RETURN = 0x0D;
constexpr int VK_SHIFT = 0x10;
constexpr int VK_CONTROL = 0x11;
constexpr int VK_MENU = 0x12;
constexpr int VK_ESCAPE = 0x1B;
constexpr int VK_SPACE = 0x20;
constexpr int VK_PRIOR = 0x21;
constexpr int VK_NEXT = 0x22;
constexpr int VK_END = 0x23;
constexpr int VK_HOME = 0x24;
constexpr int VK_LEFT = 0x25;
constexpr int VK_UP = 0x26;
constexpr int VK_RIGHT = 0x27;
constexpr int VK_DOWN = 0x28;
constexpr int VK_INSERT = 0x2D;
constexpr int VK_DELETE = 0x2E;
constexpr int VK_OEM_1 = 0xBA;
constexpr int VK_OEM_PLUS = 0xBB;
constexpr int VK_OEM_COMMA = 0xBC;
constexpr int VK_OEM_MINUS = 0xBD;
constexpr int VK_OEM_PERIOD = 0xBE;
constexpr int VK_OEM_2 = 0xBF;
constexpr int VK_OEM_3 = 0xC0;
constexpr int VK_OEM_4 = 0xDB;
constexpr int VK_OEM_5 = 0xDC;
constexpr int VK_OEM_6 = 0xDD;
constexpr int VK_OEM_7 = 0xDE;
#endif

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <slc/formats/v3/atom.hpp>
#include <slc/formats/v3/replay.hpp>
#include <variant>
#include <fstream>
#include <optional>

#include "Geode/utils/string.hpp"
#include "assist/autoclicker.hpp"
#include "assist/pathfinder.hpp"
#include "assist/hitboxes.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "checkpoint/fix.hpp"
#include "hook.hpp"
#include "label/label.hpp"
#include "render/dsp.hpp"
#include "render/renderer.hpp"
#include "replay/macro.hpp"
#include "config/config.hpp"
#include "shared/keys.hpp"
#include "trajectory/trajectory.hpp"

#include "imgui.h"
#include "util/storage.hpp"
#include "imgui_helpers.hpp"

#ifdef GRAPE_PRIVATE_PC
#include "license.hpp"
#include "script_engine.hpp"
#include "skeet_menu.hpp"
#endif

#ifdef SILICATE_PROTECT
#include "VMProtect/VMProtectSDK.h"
#endif

using namespace geode::prelude;

#ifdef GRAPE_PRIVATE_PC
static std::optional<int> s_pendingMenuStyle;
#endif

UIManager::UIManager()
  : m_font(nullptr), m_medium(nullptr), m_bold(nullptr), m_menuFont(nullptr),
    m_menuMediumFont(nullptr), m_menuBoldFont(nullptr) {}

void UIManager::toggle() { m_state.toggle(); }

static std::vector<Theme> s_themes = {
    Theme("Silica", "title_new.png",
          R"(#version 130
        #extension GL_ARB_explicit_attrib_location : require
        #extension GL_ARB_explicit_uniform_location : require

        in vec2 v_texCoord;
        out vec4 fragColor;

        layout(location = 0) uniform sampler2D u_texture;
        layout(location = 1) uniform vec2 u_texelSize;
        layout(location = 2) uniform vec2 u_direction;
        layout(location = 3) uniform vec4 u_window;
        layout(location = 4) uniform float u_time;

        void main() {
            fragColor = texture2D(u_texture, v_texCoord);
        }
        )",
          1.0),
    Theme("Polychrome", "title_gay_new.png",
          R"(#version 130
        #extension GL_ARB_explicit_attrib_location : require
        #extension GL_ARB_explicit_uniform_location : require

        in vec2 v_texCoord;
        out vec4 fragColor;

        layout(location = 0) uniform sampler2D u_texture;
        layout(location = 1) uniform vec2 u_texelSize;
        layout(location = 2) uniform vec2 u_direction;
        layout(location = 3) uniform vec4 u_window;
        layout(location = 4) uniform float u_time;

        void main() {
            fragColor = vec4(
                texture2D(u_texture, v_texCoord + vec2(0.01, 0.0)).x,
                texture2D(u_texture, v_texCoord + vec2(0.0, -0.01)).y,
                texture2D(u_texture, v_texCoord + vec2(-0.01, 0.0)).z,
                1.0
            );
            fragColor = clamp(fragColor, 0.15, 1.0);

            vec2 uv = v_texCoord;

            vec4 col =  vec4(
                abs(sin(uv.y + 2.0)) * abs(sin(uv.x * 2.0 + u_time)) * 1.0,
                abs(sin(uv.y + 2.0)) * abs(sin(uv.x * 2.0 + u_time + 0.4)) * 1.0,
                abs(sin(uv.y + 2.0)) * abs(sin(uv.x * 2.0 + u_time + 0.8)) * 1.0,
                1.0
            );

            vec4 col2 =  vec4(
                abs(sin(uv.y)) * abs(sin(uv.x * 2.0 + u_time + 1.0)) * 1.0,
                abs(sin(uv.y)) * abs(sin(uv.x * 2.0 + u_time + 1.5)) * 1.0,
                abs(sin(uv.y)) * abs(sin(uv.x * 2.0 + u_time + 2.0)) * 1.0,
                1.0
            );

            vec4 rainbowCol = col + col2;

            fragColor *= rainbowCol;
        }
        )",
          3.0),
    Theme("Estradiol", "title_trans_new.png",
          R"(#version 130
        #extension GL_ARB_explicit_attrib_location : require
        #extension GL_ARB_explicit_uniform_location : require

        in vec2 v_texCoord;
        out vec4 fragColor;

        layout(location = 0) uniform sampler2D u_texture;
        layout(location = 1) uniform vec2 u_texelSize;
        layout(location = 2) uniform vec2 u_direction;
        layout(location = 3) uniform vec4 u_window;
        layout(location = 4) uniform float u_time;

        void main() {

            fragColor = texture2D(u_texture, v_texCoord);

            vec2 uv = v_texCoord;

            vec4 col =  vec4(
                abs(sin(uv.y + 2.0)) * abs(sin(uv.x * 2.0 + u_time)) * 2.0,
                abs(sin(uv.y + 2.0)) * abs(sin(uv.x * 2.0 + u_time + 0.4)) * 0.0,
                abs(sin(uv.y + 2.0)) * abs(sin(uv.x * 2.0 + u_time)) * 2.0,
                1.0
            );

            vec4 col2 =  vec4(
                abs(sin(uv.y)) * abs(sin(uv.x * 2.0 + u_time + 1.0)) * 0.0,
                abs(sin(uv.y)) * abs(sin(uv.x * 2.0 + u_time + 2.0)) * 2.0,
                abs(sin(uv.y)) * abs(sin(uv.x * 2.0 + u_time + 2.0)) * 2.0,
                1.0
            );

            vec4 rainbowCol = col + col2;

            fragColor = mix(fragColor, rainbowCol, 0.4);
        }
        )",
          1.55),
    Theme("Endothermic", "title_jolly_new.png",
          R"(#version 130
        #extension GL_ARB_explicit_attrib_location : require
        #extension GL_ARB_explicit_uniform_location : require

        #define _SnowflakeAmount 250
        #define _BlizardFactor 0.15

        in vec2 v_texCoord;
        out vec4 fragColor;

        layout(location = 0) uniform sampler2D u_texture;
        layout(location = 1) uniform vec2 u_texelSize;
        layout(location = 2) uniform vec2 u_direction;
        layout(location = 3) uniform vec4 u_window;
        layout(location = 4) uniform float u_time;

        float rnd(float x) {
            return fract(sin(dot(vec2(x+47.49,38.2467/(x+2.3)), vec2(12.9898, 78.233)))* (43758.5453));
        }

        float drawCircle(vec2 center, float radius) {
            vec2 uv = v_texCoord * vec2(1.0, u_texelSize.x / u_texelSize.y);

            return 1.0 - smoothstep(0.0, radius, length(uv - center));
        }

        void main() {
            fragColor = texture2D(u_texture, v_texCoord);

            vec2 uv = v_texCoord;
            fragColor += vec4(0.2, 0.2, 0.6, 1.0);

            for (int i=0; i < _SnowflakeAmount; i++) {

                float j = float(i);
                float speed = 0.3+rnd(cos(j))*(0.7+0.5*cos(j/(float(_SnowflakeAmount)*0.25)));

                vec2 center = vec2((0.25-uv.y)*_BlizardFactor+rnd(j)+0.1*cos(u_time+sin(j)), mod(sin(j)-speed*(u_time*1.5*(0.1+_BlizardFactor)), 0.65));

                float circle = drawCircle(center, 0.001 + speed * 0.012) * ((0.001 + speed * 0.012) * 50.0);

                fragColor += vec4(circle * 0.4, circle * 0.4, circle * 0.5, circle * 0.35);

            }
        }
        )",
          1.0),
    Theme("Cyanide", "title_new.png",
          R"(#version 130
        #extension GL_ARB_explicit_attrib_location : require
        #extension GL_ARB_explicit_uniform_location : require

        in vec2 v_texCoord;
        out vec4 fragColor;

        layout(location = 0) uniform sampler2D u_texture;
        layout(location = 1) uniform vec2 u_texelSize;
        layout(location = 2) uniform vec2 u_direction;
        layout(location = 3) uniform vec4 u_window;
        layout(location = 4) uniform float u_time;

        void main() {
            fragColor = texture2D(u_texture, v_texCoord);
            float gray = 0.299 * fragColor.x + 0.587 * fragColor.y + 0.114 * fragColor.z;
            fragColor = mix(vec4(gray, gray, gray, 1.0), fragColor, 0.3);
            fragColor -= vec4(0.3);

            vec2 uv = v_texCoord;

            float n = u_time;
            float x = uv.x * (sin(uv.y + u_time * 0.5)* 2.0);
            float y = uv.y * (sin(uv.x + u_time * 0.2)* 2.0);

            float xp = uv.x-0.5+sin(x*3.+n-sin(y*7.+n));
            float yp = uv.y-0.5+sin(y*3.+n+sin(x*5.-n));

            float eh = ((sqrt(xp*xp+yp*yp)*5.+n));

            vec3 one = vec3(0.2, 0.7, 0.5) * 0.8;
            vec3 two = vec3(0.9, 0.2, 0.5) * 0.8;

            vec4 phase = vec4(mix(one, two, (sin(u_time) + 1.0) / 2.0), 1.0);

            fragColor += phase;
            fragColor += vec4(
                sin(eh*0.6+(y+n)*5.-n*5.),
                sin(eh*0.6+(y+n)*5.-n*5.),
                sin(eh*0.6+(y+n)*5.-n*5.),
            1.0) * vec4(one, 1.0) * vec4(0.4);

            fragColor += vec4(
                sin(eh*0.5+(y+n*1.1)*5.-n*5.),
                sin(eh*0.5+(y+n*1.1)*5.-n*5.),
                sin(eh*0.5+(y+n*1.1)*5.-n*5.),
            1.0) * vec4(two, 1.0) * vec4(0.4);

            float light = 0.299 * fragColor.x + 0.587 * fragColor.y + 0.114 * fragColor.z;
            vec4 mixed = (fragColor * vec4(mix(two, one, (sin(u_time) + 1.0) / 2.0), 1.0));
            fragColor = mix(fragColor, mixed, clamp(light - 0.6, 0.0, 1.0));

        }
        )",
          2.0),
};

static uint64_t fnv1aHash(const std::vector<uint8_t>& data) {
    uint64_t hash = 14695981039346656037ull;

    for (const uint8_t byte : data) {
        hash ^= byte;
        hash *= 1099511628211;
    }

    return hash;
}

#ifdef GEODE_IS_WINDOWS
using DpiGetterType = decltype(GetDpiForWindow)*;
static DpiGetterType g_dpiGetter = nullptr;
static float getWindowDpi() {
    if (!g_dpiGetter) return 1.0;

    uint32_t dpi = g_dpiGetter(ImGuiHookCtx::get().m_hWnd);
    float scaling = (float)dpi / 96.0;

    return scaling;
}
#else
static float getWindowDpi() { return 1.0f; }
#endif

static std::string ffmpegUrl = "https://cdn.silicate.dev/ffmpeg.zip";

static void applyAmethystStyle() {
    auto& style = ImGui::GetStyle();
    auto* colors = style.Colors;
    style.WindowPadding = ImVec2(6.0f, 6.0f);
    style.FramePadding = ImVec2(4.0f, 3.0f);
    style.CellPadding = ImVec2(4.0f, 3.0f);
    style.ItemSpacing = ImVec2(4.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = style.PopupRounding = style.FrameRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = style.TabRounding = 0.0f;

    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.90f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.50f, 0.60f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.07f, 0.12f, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.09f, 0.14f, 0.72f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.07f, 0.12f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.20f, 0.35f, 0.80f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.12f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.20f, 0.38f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.25f, 0.55f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.09f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.14f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.07f, 0.05f, 0.10f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.45f, 0.40f, 0.65f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.65f, 0.45f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.50f, 0.35f, 0.75f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.65f, 0.45f, 0.95f, 1.00f);
    colors[ImGuiCol_Button] = colors[ImGuiCol_Header] = ImVec4(0.25f, 0.20f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = colors[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.28f, 0.62f, 1.00f);
    colors[ImGuiCol_ButtonActive] = colors[ImGuiCol_HeaderActive] = ImVec4(0.50f, 0.35f, 0.80f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.12f, 0.25f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.38f, 0.28f, 0.62f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.28f, 0.20f, 0.45f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.50f, 0.35f, 0.80f, 0.35f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.60f, 0.45f, 0.90f, 1.00f);
}

static void pushDefaultMenuStyle() {
    const auto color = [](int r, int g, int b, float alpha = 1.0f) {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, alpha);
    };
    const ImVec4 surface = color(0x18, 0x15, 0x18);
    const auto& savedAccent = GrapeSettings::get()->grapeAccent;
    const ImVec4 accent(savedAccent[0], savedAccent[1], savedAccent[2],
                        savedAccent[3]);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, surface);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, surface);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, surface);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, surface);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, color(0x21, 0x1d, 0x21));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, color(0x2a, 0x25, 0x2a));
    ImGui::PushStyleColor(ImGuiCol_Button, surface);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color(0x21, 0x1d, 0x21));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color(0x2a, 0x25, 0x2a));
    ImGui::PushStyleColor(ImGuiCol_Header, surface);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color(255, 255, 255, .20f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, color(255, 255, 255, .26f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, color(255, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, color(0x1c, 0x1a, 0x1d));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, color(0x1c, 0x1a, 0x1d));
    ImGui::PushStyleColor(ImGuiCol_Tab, surface);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, color(255, 255, 255, .20f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, color(255, 255, 255, .14f));
    ImGui::PushStyleColor(ImGuiCol_NavHighlight, accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, accent);
    ImGui::PushStyleColor(
        ImGuiCol_SliderGrabActive,
        ImVec4(std::min(accent.x * 1.15f, 1.0f),
               std::min(accent.y * 1.15f, 1.0f),
               std::min(accent.z * 1.15f, 1.0f), accent.w));
    ImGui::PushStyleColor(ImGuiCol_Border, color(0x22, 0x22, 0x22));
    ImGui::PushStyleColor(ImGuiCol_Separator, color(0x22, 0x22, 0x22));
}

static void popDefaultMenuStyle() {
    ImGui::PopStyleColor(23);
    ImGui::PopStyleVar(14);
}

static bool defaultMenuTab(const char* label, bool selected) {
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(textSize.x + 12.0f, ImGui::GetFrameHeight());
    const bool pressed = ImGui::InvisibleButton(label, size);
    const float active = slui::animate_last_item(selected ? 1.0f : 0.0f);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 textPos(min.x + 6.0f,
                         min.y + (size.y - textSize.y) * .5f - 1.0f);
    const int text = static_cast<int>(128.0f + 127.0f * active);
    draw->AddText(textPos, IM_COL32(text, text, text, 255), label);
    if (active > .001f) {
        ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight);
        accent.w *= active;
        draw->AddRectFilled(
            ImVec2(textPos.x, min.y + size.y - 2.0f),
            ImVec2(textPos.x + textSize.x, min.y + size.y),
            ImGui::GetColorU32(accent), 1.0f);
    }
    return pressed;
}

static bool saveImGuiTheme(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    const auto& s = ImGui::GetStyle();
    out << "GrapeTheme 1\n" << s.Alpha << ' ' << s.DisabledAlpha << '\n';
    out << s.WindowRounding << ' ' << s.ChildRounding << ' ' << s.PopupRounding
        << ' ' << s.FrameRounding << ' ' << s.ScrollbarRounding << ' '
        << s.GrabRounding << ' ' << s.TabRounding << '\n';
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        const auto& c = s.Colors[i];
        out << c.x << ' ' << c.y << ' ' << c.z << ' ' << c.w << '\n';
    }
    return out.good();
}

static bool loadImGuiTheme(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (!in || (magic != "GrapeTheme" && magic != "SilicateTheme") ||
        version != 1)
        return false;
    auto& s = ImGui::GetStyle();
    in >> s.Alpha >> s.DisabledAlpha;
    in >> s.WindowRounding >> s.ChildRounding >> s.PopupRounding
       >> s.FrameRounding >> s.ScrollbarRounding >> s.GrabRounding
       >> s.TabRounding;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        auto& c = s.Colors[i];
        in >> c.x >> c.y >> c.z >> c.w;
    }
    return in.good() || in.eof();
}

static std::pair<bool, bool> drawSkeetActionRow(
    const char* id, const char* left, const char* right) {
    bool leftPressed = false;
    bool rightPressed = false;
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame,
                          ImVec2(220.0f, 0.0f))) {
        ImGui::TableNextColumn();
        leftPressed = slui::raw_button(left, ImVec2(-FLT_MIN, 0));
        ImGui::TableNextColumn();
        rightPressed = slui::raw_button(right, ImVec2(-FLT_MIN, 0));
        ImGui::EndTable();
    }
    return {leftPressed, rightPressed};
}

static void drawImGuiThemeEditor(ImFont* headingFont) {
    if (slui::Config::get().customMode) {
        auto* settings = GrapeSettings::get();
        static slui::ColorState accent;
        accent.colors = settings->grapeAccent;
        slui::text("Menu Accent", headingFont);
        if (slui::color("Accent", accent).changed)
            settings->grapeAccent = accent.colors;
        return;
    }
    if (slui::Config::get().skeetMode) {
        auto* settings = GrapeSettings::get();
        static slui::ColorState accent;
        static slui::ColorState gradientLeft;
        static slui::ColorState gradientMiddle;
        static slui::ColorState gradientRight;
        accent.colors = settings->skeetAccent;
        gradientLeft.colors = settings->skeetGradientLeft;
        gradientMiddle.colors = settings->skeetGradientMiddle;
        gradientRight.colors = settings->skeetGradientRight;

        slui::divider(false);
        slui::text("Colors", headingFont);
        slui::color("Accent", accent);
        slui::color("Gradient Left", gradientLeft);
        slui::color("Gradient Middle", gradientMiddle);
        slui::color("Gradient Right", gradientRight);

        settings->skeetAccent = accent.colors;
        settings->skeetGradientLeft = gradientLeft.colors;
        settings->skeetGradientMiddle = gradientMiddle.colors;
        settings->skeetGradientRight = gradientRight.colors;
        return;
    }

    static std::string name = "custom";
    slui::text("Theme", headingFont);
    ImGui::Separator();
    std::string safeName(name);
    safeName.erase(std::remove_if(safeName.begin(), safeName.end(), [](char c) {
        return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
               c == '\"' || c == '<' || c == '>' || c == '|';
    }), safeName.end());
    const auto path = grape::paths::directory("themes") / (safeName + ".theme");
    if (ImGui::BeginTable(
                   "ThemeSaveLoad", 4, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Load", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##ThemeName", "Theme name", &name);
        ImGui::TableNextColumn();
        if (slui::raw_button("Save")) saveImGuiTheme(path);
        ImGui::TableNextColumn();
        if (slui::raw_button("Load")) loadImGuiTheme(path);
        ImGui::TableNextColumn();
        if (slui::raw_button("Folder"))
            geode::utils::file::openFolder(grape::paths::directory("themes"));
        ImGui::EndTable();
    }
    ImGui::Checkbox("Docked", &GrapeSettings::get()->fitMenuToContent);

    auto& s = ImGui::GetStyle();
    ImGui::SeparatorText("Alpha");
    ImGui::SliderFloat("Global alpha", &s.Alpha, 0.1f, 1.0f);
    ImGui::SliderFloat("Disabled alpha", &s.DisabledAlpha, 0.0f, 1.0f);
    ImGui::SeparatorText("Rounding");
    ImGui::SliderFloat("Window", &s.WindowRounding, 0.0f, 24.0f);
    ImGui::SliderFloat("Child", &s.ChildRounding, 0.0f, 24.0f);
    ImGui::SliderFloat("Popup", &s.PopupRounding, 0.0f, 24.0f);
    ImGui::SliderFloat("Frame", &s.FrameRounding, 0.0f, 24.0f);
    ImGui::SliderFloat("Scrollbar", &s.ScrollbarRounding, 0.0f, 24.0f);
    ImGui::SliderFloat("Grab", &s.GrabRounding, 0.0f, 24.0f);
    ImGui::SliderFloat("Tab", &s.TabRounding, 0.0f, 24.0f);
    ImGui::SeparatorText("Colors");
    if (ImGui::BeginTable("ThemeColors", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < ImGuiCol_COUNT; ++i) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::ColorEdit4(ImGui::GetStyleColorName(i), &s.Colors[i].x,
                              ImGuiColorEditFlags_AlphaBar);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void UIManager::setup() {
    applyAmethystStyle();
    m_state.m_animationSpeed->handle([](float& speed) {
        if (speed < 0.1f || speed > 3.0f) {
            speed = 1.0f;
        }

        slui::Config::get().animationSpeed = speed;
    });

    m_state.m_playAnimations->handle(
        [](bool& play) { slui::Config::get().playAnimations = play; });

#ifdef GEODE_IS_WINDOWS
    g_dpiGetter = reinterpret_cast<DpiGetterType>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow")));
#endif

    m_state.m_uiScale->handle([this](float& scale) {
        slui::Config::get().uiScale = scale * getWindowDpi();
        m_state.m_restartGameInfo = true;
    });

    slui::Config::get().uiScale =
        m_state.m_uiScale->inner() * geode::utils::getDisplayFactor();

    static ImVector<ImWchar> glyphRanges;

    ImFontGlyphRangesBuilder builder;
    builder.AddChar(0xf192);
    builder.AddChar(0xefba);
    builder.AddChar(0xf03d);
    builder.AddChar(0xf121);
    builder.AddChar(0xf013);
    builder.AddChar(0xf078);
    builder.AddChar(0xf044);
    builder.AddChar(0xf054);
    builder.AddChar(0xf00c);
    builder.AddChar(0xf51b);
    builder.AddChar(0xf004);
    builder.BuildRanges(&glyphRanges);

    ImFontConfig mediumFontCfg;
    mediumFontCfg.OversampleH = 3;
    mediumFontCfg.OversampleV = 3;
    
    mediumFontCfg.GlyphExtraAdvanceX = -1.0f * 20.0f * 0.02f;

    ImFont* mediumFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        geode::utils::string::pathToString(Mod::get()->getResourcesDir() /
                                           "font_medium.ttf")
            .c_str(),
        20.0f, &mediumFontCfg);
    mediumFontCfg.MergeMode = true;
    mediumFontCfg.GlyphOffset = ImVec2(0.0f, -1.0f);
    ImGui::GetIO().Fonts->AddFontFromFileTTF(
        geode::utils::string::pathToString(Mod::get()->getResourcesDir() /
                                           "font_symbols.ttf")
            .c_str(),
        18.0f, &mediumFontCfg, glyphRanges.Data);

    ImFontConfig mainFontCfg;
    mainFontCfg.OversampleH = 3;
    mainFontCfg.OversampleV = 3;
    
    mainFontCfg.GlyphExtraAdvanceX = -1.0f * 17.0f * 0.03f;

    ImFont* mainFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        geode::utils::string::pathToString(Mod::get()->getResourcesDir() /
                                           "font_main.ttf")
            .c_str(),
        17.0f, &mainFontCfg);
    mainFontCfg.MergeMode = true;
    mainFontCfg.GlyphOffset = ImVec2(0.0f, -2.0f);
    ImGui::GetIO().Fonts->AddFontFromFileTTF(
        geode::utils::string::pathToString(Mod::get()->getResourcesDir() /
                                           "font_symbols.ttf")
            .c_str(),
        14.0f, &mainFontCfg, glyphRanges.Data);

    m_font   = mainFont;
    m_medium = mediumFont;

    m_bold = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        geode::utils::string::pathToString(
            Mod::get()->getResourcesDir() / "font_bold.ttf").c_str(), 32.0f);

#ifdef GRAPE_PRIVATE_PC
    const auto menuFontPath = geode::utils::string::pathToString(
        Mod::get()->getResourcesDir() / "Arboria-Medium.ttf");
    const auto loadMenuFont = [&](float size) {
        ImFontConfig config;
        config.OversampleH = 3;
        config.OversampleV = 3;
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(
            menuFontPath.c_str(), size, &config);
    };
    m_menuFont = loadMenuFont(14.0f);
    m_menuMediumFont = loadMenuFont(16.0f);
    m_menuBoldFont = loadMenuFont(24.0f);
    for (auto* font : {m_menuFont, m_menuMediumFont, m_menuBoldFont}) {
        if (!font) continue;
        for (ImWchar upper = 'A'; upper <= 'Z'; ++upper)
            font->AddRemapChar(upper, upper - 'A' + 'a');
    }
    if (m_menuFont) {
        ImFontConfig symbols;
        symbols.MergeMode = true;
        symbols.GlyphOffset = ImVec2(0.0f, -2.0f);
        ImGui::GetIO().Fonts->AddFontFromFileTTF(
            geode::utils::string::pathToString(
                Mod::get()->getResourcesDir() / "font_symbols.ttf")
                .c_str(),
            14.0f, &symbols, glyphRanges.Data);
    }
    if (!m_menuFont) m_menuFont = m_font;
    if (!m_menuMediumFont) m_menuMediumFont = m_medium;
    if (!m_menuBoldFont) m_menuBoldFont = m_bold;
    grape::pc::setupSkeetFonts();
    slui::Config::get().skeetHeaderFont = grape::pc::skeetHeaderFont();
    ImGui::GetIO().Fonts->Build();
#endif

    {
        namespace fs = std::filesystem;
        auto fontsDir = grape::paths::directory("fonts");
        fs::create_directories(fontsDir);

        m_state.m_customFontNames.clear();
        m_state.m_customFontFiles.clear();

        // Don't offer custom fonts where they can't be drawn -- picking one
        // used to crash the game outright. See labelFontsSupported().
        if (labelFontsSupported()) {
            for (const auto& entry : fs::directory_iterator(fontsDir)) {
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (ext != ".ttf" && ext != ".otf") continue;

                m_state.m_customFontNames.push_back(
                    entry.path().stem().string());
                m_state.m_customFontFiles.push_back(
                    entry.path().filename().string());
            }
        }

        m_state.m_labelFontsState.options = {"Big", "Regular"};
        m_state.m_labelFontsState.options.insert(
            m_state.m_labelFontsState.options.end(),
            m_state.m_customFontNames.begin(),
            m_state.m_customFontNames.end());
    }

    m_state.m_rainbow->notifyChange();
    m_state.m_playAnimations->notifyChange();
    m_state.m_animationSpeed->notifyChange();

    for (const auto& label : GrapeEngine::get()->labels().m_labels) {
        m_state.m_labelState.options.push_back(label.getFriendlyName().c_str());
    }
    m_state.m_labelState.selectedIndex = 0;

    m_state.m_replayNames.clear();
    for (const auto& entry : std::filesystem::directory_iterator(
              grape::paths::directory("replays"))) {
        if (entry.is_regular_file() &&
            (entry.path().extension() == ".grape" ||
             entry.path().extension() == ".slc")) {
            m_state.m_replayNames.push_back(entry.path().stem().string());
        }
    }

    m_state.m_presetNames.clear();
    for (const auto& entry : std::filesystem::directory_iterator(
              grape::paths::directory("presets"))) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            m_state.m_presetNames.push_back(entry.path().stem().string());
        }
    }

    m_state.m_scriptNames.clear();
    for (const auto& entry : std::filesystem::directory_iterator(
              grape::paths::directory("scripts"))) {
        if (entry.is_regular_file() && entry.path().extension() == ".lua") {
            m_state.m_scriptNames.push_back(entry.path().stem().string());
        }
    }

    m_replayAutocomplete.suggestions = m_state.m_replayNames;
    m_state.m_presetAutocomplete.suggestions = m_state.m_presetNames;
    m_state.m_scriptAutocomplete.suggestions = m_state.m_scriptNames;
#ifdef GRAPE_PRIVATE_PC
    auto& scripts = grape::pc::ScriptEngine::get();
    scripts.refresh();
    for (const auto& script : scripts.scripts()) scripts.load(script.name);
#endif

    m_state.m_bgColorState.colors = GrapeSettings::get()->layoutBgColor;
    m_state.m_groundColorState.colors = GrapeSettings::get()->layoutGroundColor;
    m_state.m_holdingTrailColorState.colors =
        GrapeSettings::get()->hitboxes.holdingTrailColor;
    m_state.m_noclipTintColorState.colors =
        GrapeSettings::get()->noclipTintColor;

    for (auto& theme : s_themes) {
        theme.initialize();
    }

    m_theme = &s_themes[GrapeSettings::get()->theme];
    m_state.m_themeState.selectedIndex = GrapeSettings::get()->theme;
    m_theme->apply();

    for (const auto& theme : s_themes) {
        m_state.m_themeState.options.push_back(theme.m_name);
    }

    Renderer::get()->loadFFmpeg();
}

static void preDrawBlurEffect(const ImDrawList*, const ImDrawCmd* cmd) {
    auto pos = ImGui::GetDrawData()->DisplayPos;
    auto sz = ImGui::GetDrawData()->DisplaySize;

    ImGuiHookCtx::get().preSampleBlur(
        ImVec4((cmd->ClipRect.x - pos.x) / sz.x,
               1.0 - ((cmd->ClipRect.w - pos.y) / sz.y),
               (cmd->ClipRect.z - pos.x) / sz.x,
               1.0 - ((cmd->ClipRect.y - pos.y) / sz.y)));
}

static void drawBlurEffectFirstPass(const ImDrawList*, const ImDrawCmd*) {
    ImGuiHookCtx::get().sampleBlurFirstPass();
}

static void drawBlurEffectSecondPass(const ImDrawList*, const ImDrawCmd*) {
    ImGuiHookCtx::get().sampleBlurSecondPass();
}

static void drawPostprocess(const ImDrawList*, const ImDrawCmd*) {
    ImGuiHookCtx::get().sampleBlurPostprocess();
}

static void postDrawBlurEffect(const ImDrawList*, const ImDrawCmd*) {
    ImGuiHookCtx::get().postSampleBlur();
}

static void renderBlurBg(float rounding = 24.0f, float borderSize = 2.5f,
                         bool useShader = true, float bgOpacity = 0.15f,
                         bool pp = false) {
#ifdef GEODE_IS_MOBILE
    useShader = false;
#endif
    rounding *= slui::Config::get().uiScale;
    borderSize *= slui::Config::get().uiScale;

    bgOpacity = std::pow(bgOpacity, GrapeEngine::get()->ui().m_theme->m_opacityExp);

    if (!useShader) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetWindowPos(),
            ImGui::GetWindowPos() + ImGui::GetWindowSize(),
            ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, bgOpacity)), rounding,
            ImDrawFlags_RoundCornersAll);

        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetWindowPos(),
            ImGui::GetWindowPos() + ImGui::GetWindowSize(),
            ImGui::GetColorU32(ImVec4(1.0, 1.0, 1.0, 0.2f)), rounding,
            ImDrawFlags_RoundCornersAll, borderSize);

        return;
    }

    ImGuiHookCtx::get().m_renderData.m_size =
        cocos2d::CCSize(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    ImGuiHookCtx::get().m_renderData.m_pos =
        cocos2d::CCPoint(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    cocos2d::CCSize frameSize = cocos2d::CCSize(ImGuiHookCtx::get().m_width,
                                                ImGuiHookCtx::get().m_height);

    drawList->AddCallback(&preDrawBlurEffect, nullptr);

    ImVec2 origin_pos = ImGui::GetWindowPos();
    ImVec2 dest_pos = ImGui::GetWindowPos() + ImGui::GetWindowSize();

    float origin_uv_x = ImGui::GetWindowPos().x /
                        ImGuiHookCtx::get().m_blurPass.m_width /
                        ImGuiHookCtx::BLUR_DOWNSCALING_FACTOR;
    float origin_uv_y = (frameSize.height - ImGui::GetWindowPos().y) /
                        ImGuiHookCtx::get().m_blurPass.m_height /
                        ImGuiHookCtx::BLUR_DOWNSCALING_FACTOR;

    float dest_uv_x = (ImGui::GetWindowPos().x + ImGui::GetWindowSize().x) /
                      ImGuiHookCtx::get().m_blurPass.m_width /
                      ImGuiHookCtx::BLUR_DOWNSCALING_FACTOR;
    float dest_uv_y = (frameSize.height - ImGui::GetWindowPos().y -
                       ImGui::GetWindowSize().y) /
                      ImGuiHookCtx::get().m_blurPass.m_height /
                      ImGuiHookCtx::BLUR_DOWNSCALING_FACTOR;

    const int SAMPLE_COUNT = 8;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        drawList->AddCallback(&drawBlurEffectFirstPass, nullptr);
        drawList->AddImage(
            ImTextureRef((ImTextureID)ImGuiHookCtx::get().m_blurPass.m_tex),
            {-1.0, -1.0}, {1.0, 1.0});

        if (i == SAMPLE_COUNT - 1 && pp) {
            drawList->AddCallback(&drawPostprocess, nullptr);
        } else {
            drawList->AddCallback(&drawBlurEffectSecondPass, nullptr);
        }

        drawList->AddImage(
            ImTextureRef((ImTextureID)ImGuiHookCtx::get().m_inputTex),
            {-1.0, -1.0}, {1.0, 1.0});
    }

    drawList->AddCallback(&postDrawBlurEffect, nullptr);
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    drawList->AddImageRounded(
        ImTextureRef((ImTextureID)ImGuiHookCtx::get().m_blurPass.m_tex),
        origin_pos, dest_pos, {origin_uv_x, origin_uv_y},
        {dest_uv_x, dest_uv_y}, ImGui::GetColorU32(ImVec4(1.0, 1.0, 1.0, 1.0)),
        rounding, ImDrawFlags_RoundCornersAll);

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(),
        ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, bgOpacity)), rounding,
        ImDrawFlags_RoundCornersAll);

    ImGui::GetWindowDrawList()->AddRect(
        ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(),
        ImGui::GetColorU32(ImVec4(1.0, 1.0, 1.0, 0.1f)), rounding,
        ImDrawFlags_RoundCornersAll, borderSize);
}

static std::vector<std::string> filterCandidates(
    const std::vector<std::string>& candidates, const std::string& filter) {
    std::vector<std::string> filtered;
    std::string lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(),
                   ::tolower);

    for (const auto& candidate : candidates) {
        std::string lowerCandidate = candidate;
        std::transform(lowerCandidate.begin(), lowerCandidate.end(),
                       lowerCandidate.begin(), ::tolower);
        if (lowerCandidate.find(lowerFilter) == 0) {
            filtered.push_back(candidate);
        }
    }

    return filtered;
}

static std::string keyCodeName(int key) {
    
    if (key >= 'A' && key <= 'Z') return std::string(1, static_cast<char>(key));
    if (key >= '0' && key <= '9') return std::string(1, static_cast<char>(key));
    
    if (key >= 0x70 && key <= 0x7B) return "F" + std::to_string(key - 0x70 + 1);
    switch (key) {
        case VK_SPACE:    return "Space";
        case VK_RETURN:   return "Enter";
        case VK_ESCAPE:   return "Escape";
        case VK_TAB:      return "Tab";
        case VK_BACK:     return "Backspace";
        case VK_DELETE:   return "Delete";
        case VK_INSERT:   return "Insert";
        case VK_HOME:     return "Home";
        case VK_END:      return "End";
        case VK_PRIOR:    return "PageUp";
        case VK_NEXT:     return "PageDown";
        case VK_LEFT:     return "Left";
        case VK_RIGHT:    return "Right";
        case VK_UP:       return "Up";
        case VK_DOWN:     return "Down";
        case VK_CONTROL:  return "Ctrl";
        case VK_SHIFT:    return "Shift";
        case VK_MENU:     return "Alt";
        case VK_OEM_3:    return "`";
        case VK_OEM_MINUS:return "-";
        case VK_OEM_PLUS: return "=";
        case VK_OEM_4:    return "[";
        case VK_OEM_6:    return "]";
        case VK_OEM_5:    return "\\";
        case VK_OEM_1:    return ";";
        case VK_OEM_7:    return "'";
        case VK_OEM_COMMA:return ",";
        case VK_OEM_PERIOD:return ".";
        case VK_OEM_2:    return "/";
        default:          return "Key(" + std::to_string(key) + ")";
    }
}

static std::string keybindDisplayName(const std::shared_ptr<KeybindControl>& kb) {
    int hash = kb->getHash();
    int key  = hash & 0xFFFFF;
    int mods = hash >> 20;

    std::string name;
    if (mods & GrapeKeybind<bool>::MODIFIER_CTRL)  name += "Ctrl+";
    if (mods & GrapeKeybind<bool>::MODIFIER_SHIFT) name += "Shift+";
    if (mods & GrapeKeybind<bool>::MODIFIER_ALT)   name += "Alt+";
    name += keyCodeName(key);
    return name;
}

static std::string keybindDisplayName_fromParts(int key, int mods) {
    std::string name;
    if (mods & GrapeKeybind<bool>::MODIFIER_CTRL)  name += "Ctrl+";
    if (mods & GrapeKeybind<bool>::MODIFIER_SHIFT) name += "Shift+";
    if (mods & GrapeKeybind<bool>::MODIFIER_ALT)   name += "Alt+";
    name += keyCodeName(key);
    return name;
}

void UIManager::drawKeybindContextMenu() {
    auto* bm  = BindingManager::get();
    auto& ctx = m_state.m_keybindCtx;
    const auto verticalSpacer = [](float height) {
        ImGui::Dummy(ImVec2(0.0f, height * slui::Config::get().uiScale));
    };

    if (ctx.capturing) {
        if (bm->hasNewKey()) {
            ctx.capturedKey = static_cast<int>(bm->getNewKey());
            ctx.capturing   = false;
            ctx.open        = true;
        } else {
            const float scale = slui::Config::get().uiScale;
            const ImVec2 winSize{330.0f * scale, 0.0f};
            ImGui::SetNextWindowPos(
                ImGui::GetMainViewport()->GetCenter(),
                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(1.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg,
                                  ImVec4(0.067f, 0.067f, 0.067f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            if (ImGui::Begin("##KeyCapture", nullptr,
                             ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoScrollbar)) {
                const float contentWidth = ImGui::GetContentRegionAvail().x;
                slui::group([&]() {
                    verticalSpacer(8.0f);
                    slui::text("Keybind Capture", m_bold);
                    verticalSpacer(6.0f);
                    slui::text("Press any key on your keyboard...");
                    verticalSpacer(8.0f);
                    if (slui::button("Close").pressed) {
                        ctx.capturing = false;
                        ctx.open      = true;
                        bm->wantNewKey();
                        bm->getNewKey();
                    }
                    verticalSpacer(6.0f);
                }, contentWidth);
            }
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            return;
        }
    }

    if (!ctx.open) return;

    const float scale = slui::Config::get().uiScale;
    const ImVec2 winSize{330.0f * scale, 0.0f};
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(winSize, ImVec2(winSize.x, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0.067f, 0.067f, 0.067f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    if (!ImGui::Begin("##KeybindWindow", &ctx.open,
                      ImGuiWindowFlags_NoDecoration |
                      ImGuiWindowFlags_NoNav |
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        return;
    }

    const float contentWidth = ImGui::GetContentRegionAvail().x;
    slui::group([&]() {
        verticalSpacer(8.0f);
        slui::text("Keybinds", m_bold);
        verticalSpacer(4.0f);
        slui::text(ctx.tag);
        ImGui::Separator();

        auto existing = bm->getKeybindsForTag(ctx.tag);
        if (existing.empty()) {
            slui::text("No keybinds assigned.");
            verticalSpacer(4.0f);
        } else {
            for (auto& kb : existing) {
                std::string displayName = keybindDisplayName(kb);
                slui::text(displayName);
                verticalSpacer(4.0f);
                if (slui::button(("Remove##" + displayName)).pressed) {
                    bm->removeKeybind(kb);
                    auto path = grape::paths::file("keybinds.json");
                    bm->writeToFile(path);
                    break;
                }
                verticalSpacer(6.0f);
            }
            verticalSpacer(4.0f);
        }

        ImGui::Separator();
        slui::text("Add Keybind", m_medium);
        verticalSpacer(6.0f);

        std::string keyLabel = ctx.capturedKey == 0
            ? "Capture key..."
            : keybindDisplayName_fromParts(ctx.capturedKey, ctx.capturedMod);

        if (slui::button(keyLabel).pressed) {
            ctx.capturedKey = 0;
            ctx.capturedMod = 0;
            ctx.capturing   = true;
            ctx.open        = false;
            bm->wantNewKey();
        }

        verticalSpacer(8.0f);
        ImGui::Separator();
        verticalSpacer(4.0f);

        bool canAdd = ctx.capturedKey != 0 && bm->hasValue(ctx.tag);
        if (canAdd) {
            const float footerButtonWidth =
                (contentWidth - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
            slui::group([&]() {
                if (slui::button("Add Keybind").pressed) {
                    RawKeybind raw{
                        ctx.capturedKey, ctx.capturedMod,
                        ctx.tag, ctx.pendingType, true, ctx.pendingValue};
                    bm->addKeybindForTag(ctx.tag, raw);
                    auto path = grape::paths::file("keybinds.json");
                    bm->writeToFile(path);
                    ctx.capturedKey  = 0;
                    ctx.capturedMod  = 0;
                    ctx.pendingValue = "1";
                }
            }, footerButtonWidth);
            slui::same_line();
            slui::group([&]() {
                if (slui::button("Close").pressed) ctx.open = false;
            }, footerButtonWidth);
        } else {
            if (slui::button("Close").pressed) ctx.open = false;
        }

        verticalSpacer(8.0f);
    }, contentWidth);

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void UIManager::draw() {
#ifdef SILICATE_PROTECT
    VMProtectBegin("UIDraw");
#endif

    auto bot = GrapeEngine::get();
    auto& cfg = slui::Config::get();

#ifdef GRAPE_PRIVATE_PC
    if (s_pendingMenuStyle) {
        Mod::get()->setSavedValue("pc-menu-style", *s_pendingMenuStyle);
        s_pendingMenuStyle.reset();
    }
    const int savedMenuStyle =
        Mod::get()->getSavedValue<int>("pc-menu-style", 0);
    const bool customMenu = savedMenuStyle == 0;
    const bool skeetMenu = savedMenuStyle == 1;
#else
    const bool customMenu = false;
    const bool skeetMenu = false;
#endif
    cfg.customMode = customMenu;
    cfg.customFont = m_menuFont;
    cfg.mediumFont = m_medium;
    cfg.customMediumFont = m_menuMediumFont;
    cfg.boldFont = m_bold;
    cfg.customBoldFont = m_menuBoldFont;

#ifdef GRAPE_PRIVATE_PC
    auto& license = grape::pc::License::get();
    license.tick();
    if (!license.authorized()) {
        m_state.m_visible->inner() = true;
        CCEGLView::get()->showCursor(true);
        if (customMenu) {
            pushDefaultMenuStyle();
            slui::ScopedFont loginFont(m_font);
            license.draw(true);
            popDefaultMenuStyle();
        } else {
            license.draw(false);
        }
        return;
    }
    grape::pc::ScriptEngine::get().update(
        cocos2d::CCDirector::get()->getDeltaTime());
    grape::pc::ScriptEngine::get().overlay();
#endif

    ImGuiHookCtx::get().m_time += cocos2d::CCDirector::get()->getDeltaTime();
    m_state.m_useShader->inner() = false;
    const auto popupShaderFn = []() {};

    cfg.uiScale = m_state.m_uiScale->inner() * getWindowDpi();
    cfg.fitWindowToContent = GrapeSettings::get()->fitMenuToContent;
    static constexpr float tabHeights[] = {445, 500, 500, 410, 500, 500, 530, 530};
    cfg.fittedWindowHeight = tabHeights[static_cast<int>(m_state.m_currentTab)];

    if (!m_state.m_visible->inner()) {
#ifndef GEODE_IS_MOBILE
        auto view = CCEGLView::get();

        auto pl = PlayLayer::get();
        bool shouldHide = false;
        if (pl) {
            shouldHide = !pl->m_isPaused && !pl->m_hasCompletedLevel &&
                         !GameManager::get()->getGameVariable("0024");
        }

        view->showCursor(!shouldHide);
#endif
        return;
    }

#ifndef GEODE_IS_MOBILE
    CCEGLView::get()->showCursor(true);
#endif

#ifdef GRAPE_PRIVATE_PC
    cfg.username = license.username();
#endif
    slui::ScopedFont s(m_font);
    
    static GLuint logoTex = 0;
    static int logoWidth = 1, logoHeight = 1;
    static ImVec2 logoUv(1.0f, 1.0f);
    if (logoTex == 0) {
        auto logoPath = geode::Mod::get()->getResourcesDir() / "grape.png";
        if (!std::filesystem::exists(logoPath)) {
            logoPath = geode::Mod::get()->getResourcesDir() / "img" / "logo.png";
        }
        if (!std::filesystem::exists(logoPath)) {
            logoPath = geode::Mod::get()->getResourcesDir() / "title_new.png";
        }
        const auto path = logoPath.string();
        auto* texture = cocos2d::CCTextureCache::sharedTextureCache()->addImage(
            path.c_str(), true);
        if (texture) {
            const auto size = texture->getContentSizeInPixels();
            logoTex = texture->getName();
            logoWidth = static_cast<int>(size.width);
            logoHeight = static_cast<int>(size.height);
            logoUv = ImVec2(texture->getMaxS(), texture->getMaxT());
        } else {
            logoTex = (GLuint)-1;
        }
    }

    ImTextureID tex = (logoTex != 0 && logoTex != (GLuint)-1) ? (ImTextureID)(intptr_t)logoTex : (ImTextureID)(intptr_t)0;
#ifdef GRAPE_PRIVATE_PC
    if (customMenu) pushDefaultMenuStyle();
    else if (skeetMenu) grape::pc::pushSkeetStyle(m_state.m_opacity->inner());
#endif
    slui::Config::get().skeetMode = skeetMenu;
    slui::window(tex, ImVec2((float)logoWidth, (float)logoHeight), logoUv,
                 [this, bot, popupShaderFn,
#ifdef GRAPE_PRIVATE_PC
                  savedMenuStyle,
#endif
                  customMenu, skeetMenu]() {
        if (!skeetMenu && !customMenu) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetWindowPos(),
                ImGui::GetWindowPos() + ImGui::GetWindowSize(),
                ImGui::GetColorU32(ImVec4(
                    0.1f, 0.1f, 0.1f, m_state.m_opacity->inner())),
                // Must track the window's own rounding. Hardcoding 0 painted a
                // square over the corners the rounded window background had
                // left transparent, which is the dark notch that grew as the
                // rounding slider went up.
                ImGui::GetStyle().WindowRounding,
                ImDrawFlags_RoundCornersAll);
        }

        const auto tabButton = [this
#ifdef GRAPE_PRIVATE_PC
                                , customMenu, skeetMenu
#endif
                               ](
                                   const char* label, const char* icon,
                                   UIState::UITab tab) {
            (void)icon;
            const bool active = m_state.m_currentTab == tab;
#ifdef GRAPE_PRIVATE_PC
            if (customMenu) {
                if (defaultMenuTab(label, active))
                    m_state.m_currentTab = tab;
                ImGui::SameLine();
                return;
            }
            if (skeetMenu) {
                if (grape::pc::skeetTab(label, icon, active, 75.0f))
                    m_state.m_currentTab = tab;
                return;
            }
#endif
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));
            }
            const bool pressed = slui::raw_button(label);
            if (active) ImGui::PopStyleColor(2);
            if (pressed) m_state.m_currentTab = tab;
            if (!skeetMenu) ImGui::SameLine();
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
        if (skeetMenu) {
            ImGui::BeginChild("##skeet-sidebar", ImVec2(75.0f, 542.0f), false,
                              ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPosY(10.0f);
        }
        tabButton("Record", "\uf192", UIState::UITab::Record);
        tabButton("Assist", "\uf004", UIState::UITab::Assist);
        tabButton("Prediction", "\uefba", UIState::UITab::Prediction);
        tabButton("Edit", "\uf044", UIState::UITab::Edit);
        tabButton("Render", "\uf03d", UIState::UITab::Render);
        tabButton("Settings", "\uf013", UIState::UITab::Settings);
        tabButton("Scripts", "LUA", UIState::UITab::Scripts);
        if (skeetMenu) {
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
            ImGui::EndChild();
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::BeginGroup();
        } else {
            ImGui::NewLine();
        }
        ImGui::PopStyleVar();
        if (!skeetMenu) ImGui::Separator();

        ImGui::BeginChild(
            "Content", skeetMenu ? ImVec2(572.0f, 542.0f)
                                 : ImVec2(0.0f, 0.0f),
            false,
            customMenu && m_state.m_currentTab == UIState::UITab::Edit
                ? ImGuiWindowFlags_NoScrollbar
                : ImGuiWindowFlags_AlwaysVerticalScrollbar);
        {
            if (m_state.m_currentTab != UIState::UITab::Edit)
                m_state.m_editSelectionInitialized = false;

            if (!GrapeEngine::get()->isEnabled()) {
                slui::text("The bot is currently disabled.");
                if (GJBaseGameLayer::get()) {
                    slui::text(
                        "Please exit the level you're in to enable the bot.");
                } else {
                    if (slui::button("Enable").pressed) {
                        GrapeEngine::get()->m_enabled->inner() = true;
                        GrapeEngine::get()->m_enabled->notifyChange();
                    }
                }

                return;
            }

            slui::tab(m_state.m_currentTab, UIState::UITab::Record, [&]() {
                if (!skeetMenu && !customMenu) slui::text("Record", m_bold);

                slui::divider(false);
                if (customMenu) slui::text("Recorder", m_medium);
                auto& rs = GrapeEngine::get()->macro();

                slui::text(fmt::format("Frame: {} - Macro Size: {}",
                                       bot->timeline().getDisplayFrame(),
                                       rs.m_actionAtom.length()));

                const bool recording = bot->isRecording();
                const bool playing = bot->isPlaying();
                bool toggleRecording = false;
                bool togglePlaying = false;
                if (skeetMenu) {
                    const auto actions = drawSkeetActionRow(
                        "RecordMode",
                        recording ? "Stop Recording" : "Start Recording",
                        playing ? "Stop Playing" : "Start Playing");
                    toggleRecording = actions.first;
                    togglePlaying = actions.second;
                } else if (ImGui::BeginTable(
                               "RecordMode", 2,
                               ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    if (recording) ImGui::PushStyleColor(
                        ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    toggleRecording = slui::raw_button(
                        recording ? "Stop Recording" : "Start Recording",
                        ImVec2(-FLT_MIN, 0.0f));
                    if (recording) ImGui::PopStyleColor();

                    ImGui::TableNextColumn();
                    if (playing) ImGui::PushStyleColor(
                        ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    togglePlaying = slui::raw_button(
                        playing ? "Stop Playing" : "Start Playing",
                        ImVec2(-FLT_MIN, 0.0f));
                    if (playing) ImGui::PopStyleColor();
                    ImGui::EndTable();
                }
                if (toggleRecording) {
                    bot->setMode(recording ? GrapeEngine::Mode::Stopped
                                           : GrapeEngine::Mode::Recording);
                    if (PlayLayer::get() && bot->isRecording() &&
                        rs.getInputIndex() < rs.m_actionAtom.length()) {
                        rs.createBackup();
                        rs.m_actionAtom.clipActions(bot->timeline().getFrame());
                    }
                }
                if (togglePlaying)
                    bot->setMode(playing ? GrapeEngine::Mode::Stopped
                                        : GrapeEngine::Mode::Playing);

                slui::divider();
                if (customMenu) slui::text("Playback", m_medium);

                const auto drawTps = [&] {
                    if (customMenu) ImGui::SetNextItemWidth(80.0f);
                    if (slui::drag("TPS", bot->timeline().m_tps->inner(), 0.0,
                                   std::numeric_limits<double>::max(), 1.0f,
                                   "{:g}")
                            .changed) {
                        bot->timeline().m_tps->notifyChange();
                    }
                    keybindRightClick("updater.tps");
                };
                const auto drawSpeed = [&] {
                    if (slui::drag("Speed", bot->timeline().m_speedhack->inner(),
                                   0.0, std::numeric_limits<double>::max(),
                                   0.01f, "{:.2G}x")
                            .changed) {
                        bot->timeline().m_speedhack->notifyChange();
                    }
                    keybindRightClick("updater.speedhack");
                };
                if (skeetMenu) {
                    drawTps();
                    drawSpeed();
                    slui::checkbox("Audio Speedhack",
                                   bot->timeline().m_speedhackAudio->inner());
                    keybindRightClick("updater.speedhack_audio");
                    slui::checkbox("Block Inputs",
                                   bot->macro().m_ignoreInputs->inner());
                    keybindRightClick("replay.ignore_inputs");
                } else {
                    if (ImGui::BeginTable("RecordRate", 2,
                                          ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextColumn();
                        drawTps();
                        ImGui::TableNextColumn();
                        drawSpeed();
                        ImGui::EndTable();
                    }
                    if (ImGui::BeginTable(
                            "RecordPlaybackOptions", 2,
                            ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextColumn();
                        slui::checkbox("Audio Speedhack",
                            bot->timeline().m_speedhackAudio->inner());
                        keybindRightClick("updater.speedhack_audio");
                        ImGui::TableNextColumn();
                        slui::checkbox("Block/Ignore Inputs During Playback",
                            bot->macro().m_ignoreInputs->inner());
                        keybindRightClick("replay.ignore_inputs");
                        ImGui::EndTable();
                    }
                }

                slui::divider();
                if (customMenu) slui::text("Replays", m_medium);

                const auto replayNameInput = [&] {
                    m_replayAutocomplete.suggestions = filterCandidates(
                        m_state.m_replayNames, rs.m_replayName);
                    slui::input_text_autocomplete(
                        "##ReplayName", "Replay", rs.m_replayName,
                        m_replayAutocomplete, popupShaderFn);
                };
                bool saveReplay = false;
                bool loadReplay = false;
                if (skeetMenu) {
                    slui::next_input_full_width();
                    replayNameInput();
                    const auto actions = drawSkeetActionRow(
                        "RecordSaveLoad", "Save", "Load");
                    saveReplay = actions.first;
                    loadReplay = actions.second;
                } else if (ImGui::BeginTable(
                               "RecordSaveLoad", 3,
                               ImGuiTableFlags_None)) {
                    ImGui::TableSetupColumn(
                        "Input", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(
                        "Save", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn(
                        "Load", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    replayNameInput();
                    ImGui::TableNextColumn();
                    saveReplay = slui::raw_button("Save");
                    ImGui::TableNextColumn();
                    loadReplay = slui::raw_button("Load");
                    ImGui::EndTable();
                }
                if (saveReplay) {
                    auto path = rs.getCurrentPath();
                    rs.backupExisting(path);
                    rs.save(path);

                    m_state.m_replayNames.clear();
                    for (const auto& entry :
                         std::filesystem::directory_iterator(
                             grape::paths::directory("replays"))) {
                        if (entry.is_regular_file() &&
                            (entry.path().extension() == ".grape" ||
                             entry.path().extension() == ".slc")) {
                            m_state.m_replayNames.push_back(
                                entry.path().stem().string());
                        }
                    }
                }
                if (loadReplay) {
                    const auto dir = grape::paths::directory("replays");
                    // Resolve the actual macro file. The typed name may already
                    // include an extension, may contain dots (auto-named after a
                    // level), or exist as either .grape or .slc -- try all of
                    // these instead of blindly appending ".grape", which was
                    // failing to reload after a replay renamed the field.
                    std::filesystem::path resolved;
                    if (std::filesystem::is_regular_file(dir / rs.m_replayName)) {
                        resolved = dir / rs.m_replayName;
                    } else {
                        for (const char* ext : {".grape", ".slc"}) {
                            auto cand = dir / (rs.m_replayName + ext);
                            if (std::filesystem::is_regular_file(cand)) {
                                resolved = cand;
                                break;
                            }
                        }
                    }
                    if (resolved.empty()) {
                        geode::log::error(
                            "Failed to load replay: no file found for '{}'",
                            rs.m_replayName);
                    } else {
                        auto loaded = rs.loadSupported(resolved);
                        if (loaded.isErr())
                            geode::log::error("Failed to load replay: {}",
                                              loaded.unwrapErr());
                    }
                }

                // Rescan the replays directory every frame the tab is open so
                // newly added / saved / imported macros always show up in the
                // search list (it used to only refresh on Save or name change).
                m_state.m_lastReplayName = rs.m_replayName;
                m_state.m_replayNames.clear();
                for (const auto& entry :
                     std::filesystem::directory_iterator(
                          grape::paths::directory("replays"))) {
                    if (entry.is_regular_file() &&
                        (entry.path().extension() == ".grape" ||
                         entry.path().extension() == ".slc")) {
                        m_state.m_replayNames.push_back(
                            entry.path().stem().string());
                    }
                }

                slui::divider();
                if (customMenu) slui::text("Frame Advance", m_medium);

                if (ImGui::BeginTable(
                        "FrameAdvance", 2,
                        ImGuiTableFlags_SizingStretchSame,
                        skeetMenu ? ImVec2(220.0f, 0.0f) : ImVec2())) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Frame Advance",
                                    GrapeEngine::get()->timeline().m_paused->inner());
                    keybindRightClick("updater.frame_advance");
                    ImGui::TableNextColumn();
                    if (skeetMenu)
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
                    if (slui::raw_button(
                            "Advance", ImVec2(-FLT_MIN, 0.0f))) {
                        bot->timeline().stepOnce();
                    }
                    keybindRightClick("updater.advance_one");
                    ImGui::EndTable();
                }

                if (ImGui::BeginTable(
                        "SeedOverride", 2,
                        ImGuiTableFlags_SizingStretchSame,
                        skeetMenu ? ImVec2(220.0f, 0.0f) : ImVec2())) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Seed Override", rs.m_overrideSeed);
                    ImGui::TableNextColumn();
                    slui::drag("##Seed", rs.m_overriddenSeed, uint64_t{0},
                               std::numeric_limits<uint64_t>::max());
                    ImGui::EndTable();
                }

                slui::divider();
                if (customMenu) slui::text("Options", m_medium);

                if (skeetMenu) {
                    slui::checkbox(
                        "Intentional Death",
                        GrapeEngine::get()->timeline().m_canDie->inner());
                    keybindRightClick("updater.intentional_death");
                    slui::divider();
                    slui::checkbox(
                        "Extrapolation",
                        GrapeEngine::get()->timeline().m_extrapolateFrames->inner());
                    keybindRightClick("updater.frame_extrapolation");
                } else if (ImGui::BeginTable(
                               "DeathExtraRow", 2,
                               ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Intentional Death",
                                    GrapeEngine::get()->timeline().m_canDie->inner());
                    keybindRightClick("updater.intentional_death");
                    ImGui::TableNextColumn();
                    slui::checkbox(
                        "Extrapolation",
                        GrapeEngine::get()->timeline().m_extrapolateFrames->inner());
                    keybindRightClick("updater.frame_extrapolation");
                    ImGui::EndTable();
                }

            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Assist, [&]() {
                if (!skeetMenu && !customMenu) slui::text("Assist", m_bold);

                slui::divider(false);

                slui::text("Smart Merge", m_medium);
                bool doMerge = false;
                if (ImGui::BeginTable("SmartMergeRow", 2,
                                      ImGuiTableFlags_SizingStretchSame,
                                      skeetMenu ? ImVec2(220.0f, 0.0f)
                                                : ImVec2())) {
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    slui::input_text("##MergeReplay", "Replay to merge",
                                     m_state.m_mergeReplayName);
                    ImGui::TableNextColumn();
                    doMerge = slui::raw_button("Merge", ImVec2(-FLT_MIN, 0.0f));
                    ImGui::EndTable();
                }
                if (doMerge) {
                    auto mergePath =
                        grape::paths::directory("replays") /
                        (m_state.m_mergeReplayName + ".grape");
                    if (!std::filesystem::exists(mergePath)) {
                        mergePath.replace_extension(".slc");
                    }
                    GrapeEngine::get()->macro().merge(mergePath);
                }

                slui::divider();

                slui::text("Hitboxes", m_medium);
                slui::checkbox("Toggle##Hitboxes",
                                GrapeEngine::get()->hitboxes().m_enabled->inner());
                keybindRightClick("hitboxes.enabled");

                slui::checkbox("Show Death Collision##Hitboxes",
                                GrapeEngine::get()->hitboxes().m_showOnDeath->inner());
                keybindRightClick("hitboxes.show_on_death");

                slui::checkbox("Show Trail##Hitboxes",
                                GrapeEngine::get()->hitboxes().m_trailEnabled->inner());
                keybindRightClick("hitboxes.trail_enabled");

                if (GrapeEngine::get()->hitboxes().m_trailEnabled->inner()) {
                    auto& hitboxSettings = GrapeSettings::get()->hitboxes;
                    m_state.m_holdingTrailColorState.colors =
                        hitboxSettings.holdingTrailColor;
                    if (ImGui::BeginTable("ShowHoldingRow", 2,
                                          ImGuiTableFlags_SizingStretchSame,
                                          skeetMenu ? ImVec2(220.0f, 0.0f)
                                                    : ImVec2())) {
                        ImGui::TableNextColumn();
                        slui::checkbox("Show Holding##Hitboxes",
                                       hitboxSettings.holdingTrailEnabled);
                        ImGui::TableNextColumn();
                        slui::color("Holding Color##Hitboxes",
                                    m_state.m_holdingTrailColorState, [&]() {
                                        renderBlurBg(
                                            12.0f, 1.5f,
                                            m_state.m_useShader->inner());
                                    });
                        ImGui::EndTable();
                    }
                    hitboxSettings.holdingTrailColor =
                        m_state.m_holdingTrailColorState.colors;

                    slui::drag("Trail Length##Hitboxes",
                                hitboxSettings.trailMaxLength,
                                50, 4000, 1.0f, "{:d}");

                    slui::drag("Trail Quality##Hitboxes",
                                hitboxSettings.trailRebuildInterval,
                                1, 10, 1.0f, "Rebuild every {:d} steps");
                }

                slui::drag("Width##Hitboxes",
                            GrapeEngine::get()->hitboxes().m_width->inner(), 0.0, 1.0,
                            0.02f, "{:.2f}");

                slui::dropdown("Hitbox##Selector", m_state.m_hitboxState,
                                m_state.m_hitboxState.selectedIndex, [&]() {
                                    renderBlurBg(12.0f, 1.5f,
                                                 m_state.m_useShader->inner());
                                });

                {
                    auto& h = m_state.m_hitboxCategories[m_state.m_hitboxState
                                                             .selectedIndex];

                    auto& category = GrapeSettings::get()->hitboxes.categories[h];

                    m_state.m_hitboxColorState.colors = category.colors;

                    if (ImGui::BeginTable("SpecificHitboxRow", 2,
                                          ImGuiTableFlags_SizingStretchSame,
                                          skeetMenu ? ImVec2(220.0f, 0.0f)
                                                    : ImVec2())) {
                        ImGui::TableNextColumn();
                        slui::checkbox("Enabled##SpecificHitbox",
                                       category.enabled);
                        ImGui::TableNextColumn();
                        slui::color("Color##SpecificHitbox",
                                    m_state.m_hitboxColorState, [&]() {
                                        renderBlurBg(
                                            12.0f, 1.5f,
                                            m_state.m_useShader->inner());
                                    });
                        ImGui::EndTable();
                    }

                    category.colors = m_state.m_hitboxColorState.colors;

                    slui::drag("Fill Opacity##SpecificHitbox",
                                category.fillOpacity, 0.0, 1.0, 0.01, "{:.2f}");
                };

                slui::divider();

                slui::text("Layout", m_medium);

                if (slui::checkbox("Enabled##LayoutMode",
                                    GrapeEngine::get()->timeline().m_layoutMode->inner())
                        .pressed) {
                    GrapeEngine::get()->timeline().m_layoutMode->notifyChange();
                }
                keybindRightClick("updater.layout_mode");

                slui::checkbox(
                    "Use Regular Background##LayoutMode",
                    GrapeEngine::get()->timeline().m_useRegularBg->inner());
                keybindRightClick("updater.use_regular_bg");

                if (ImGui::BeginTable("LayoutColorRow", 2,
                                      ImGuiTableFlags_SizingStretchSame,
                                      skeetMenu ? ImVec2(220.0f, 0.0f)
                                                : ImVec2())) {
                    ImGui::TableNextColumn();
                    slui::color("Background Color##LayoutMode",
                                m_state.m_bgColorState, popupShaderFn);
                    ImGui::TableNextColumn();
                    slui::color("Ground Color##LayoutMode",
                                m_state.m_groundColorState, popupShaderFn);
                    ImGui::EndTable();
                }

                GrapeSettings::get()->layoutBgColor =
                    m_state.m_bgColorState.colors;
                GrapeSettings::get()->layoutGroundColor =
                    m_state.m_groundColorState.colors;

                slui::divider();

                slui::text("Noclip", m_medium);

                if (skeetMenu) {
                    slui::checkbox("Enabled##Noclip",
                                   GrapeEngine::get()->timeline().m_noclip->inner());
                    keybindRightClick("updater.noclip");
                    slui::dropdown(customMenu ? "##NoclipPlayer"
                                              : "Player##Noclip",
                                   m_state.m_noclipState,
                                   *reinterpret_cast<int*>(
                                       &GrapeEngine::get()->timeline().m_noclipType),
                                   popupShaderFn);
                } else if (ImGui::BeginTable(
                               "NoclipRow", 2,
                               ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Enabled##Noclip",
                                   GrapeEngine::get()->timeline().m_noclip->inner());
                    keybindRightClick("updater.noclip");
                    ImGui::TableNextColumn();
                    slui::dropdown(customMenu ? "##NoclipPlayer"
                                              : "Player##Noclip",
                                   m_state.m_noclipState,
                                   *reinterpret_cast<int*>(
                                       &GrapeEngine::get()->timeline().m_noclipType),
                                   popupShaderFn);
                    ImGui::EndTable();
                }
                GrapeSettings::get()->noclipPlayer = static_cast<int>(
                    GrapeEngine::get()->timeline().m_noclipType);

                if (ImGui::BeginTable("NoclipTintRow", 2,
                                      ImGuiTableFlags_SizingStretchSame,
                                      skeetMenu ? ImVec2(220.0f, 0.0f)
                                                : ImVec2())) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Death Tint##Noclip",
                                   GrapeSettings::get()->noclipTintEnabled);
                    ImGui::TableNextColumn();
                    if (GrapeSettings::get()->noclipTintEnabled) {
                        slui::color("Tint Color##Noclip",
                                    m_state.m_noclipTintColorState,
                                    popupShaderFn);
                        GrapeSettings::get()->noclipTintColor =
                            m_state.m_noclipTintColorState.colors;
                    }
                    ImGui::EndTable();
                }
                if (GrapeSettings::get()->noclipTintEnabled) {
                    slui::drag("Tint Opacity##Noclip",
                               GrapeSettings::get()->noclipTintOpacity, 0.0, 1.0,
                               0.01, "{:.2f}");
                    slui::drag("Tint Time##Noclip",
                               GrapeSettings::get()->noclipTintTime, 0.0, 10.0,
                               0.05, "{:.2f}");
                }

                slui::divider();

                slui::text("Trajectory", m_medium);

                slui::checkbox(
                    "Enabled##Trajectory",
                    GrapeEngine::get()->trajectory().m_state.m_enabled->inner());
                keybindRightClick("trajectory.enabled");
                slui::drag("Width##Trajectory",
                            GrapeEngine::get()->trajectory().m_state.m_width->inner(),
                            0.0, 1.0, 0.01f, "{:.2f}");
                slui::drag("Length##Trajectory",
                            GrapeEngine::get()->trajectory().m_state.m_length->inner(),
                            0.0, 5.0, 0.01f, "{:.2f}s");

                {
                    auto& maxSteps = GrapeSettings::get()->trajectory.maxSteps;
                    slui::drag("Max Steps##Trajectory", maxSteps,
                                0, 100000, 1.0f,
                                maxSteps == 0 ? "Unlimited" : "{:d} steps");
                    if (maxSteps < 0) maxSteps = 0;
                }

                auto& trajectorySettings = GrapeSettings::get()->trajectory;
                m_state.m_straightTrajectoryColorState.colors =
                    trajectorySettings.straightColor;
                if (ImGui::BeginTable("StraightTrajectoryRow", 2,
                                      ImGuiTableFlags_SizingStretchSame,
                                      skeetMenu ? ImVec2(220.0f, 0.0f)
                                                : ImVec2())) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Straight Wave##Trajectory",
                                   trajectorySettings.straightEnabled);
                    ImGui::TableNextColumn();
                    slui::color("Straight Color##Trajectory",
                                m_state.m_straightTrajectoryColorState,
                                popupShaderFn);
                    ImGui::EndTable();
                }
                trajectorySettings.straightColor =
                    m_state.m_straightTrajectoryColorState.colors;

                slui::dropdown(
                    "Trajectory##Selector", m_state.m_trajectoryState,
                    m_state.m_trajectoryState.selectedIndex, [&]() {
                        renderBlurBg(12.0f, 1.5f, m_state.m_useShader->inner());
                    });

                {
                    auto& t = m_state.m_categories[m_state.m_trajectoryState
                                                       .selectedIndex];

                    auto& category =
                        GrapeSettings::get()->trajectory.categories[t];

                    m_state.m_trajectoryColorState.colors = category.colors;

                    if (ImGui::BeginTable("SpecificTrajectoryRow", 2,
                                          ImGuiTableFlags_SizingStretchSame,
                                          skeetMenu ? ImVec2(220.0f, 0.0f)
                                                    : ImVec2())) {
                        ImGui::TableNextColumn();
                        slui::checkbox("Enabled##SpecificTrajectory",
                                       category.enabled);
                        ImGui::TableNextColumn();
                        slui::color("Color##SpecificTrajectory",
                                    m_state.m_trajectoryColorState, [&]() {
                                        renderBlurBg(
                                            12.0f, 1.5f,
                                            m_state.m_useShader->inner());
                                    });
                        ImGui::EndTable();
                    }

                    category.colors = m_state.m_trajectoryColorState.colors;
                };

                slui::divider();

                slui::text("Backstepping", m_medium);

                if (ImGui::BeginTable(
                        "BackwardsSteppingRow", 2,
                        ImGuiTableFlags_SizingStretchSame,
                        skeetMenu ? ImVec2(220.0f, 0.0f) : ImVec2())) {
                        ImGui::TableNextColumn();
                        slui::checkbox(
                            "Backwards Stepping",
                            GrapeEngine::get()->timeline().m_backwardsStepping->inner());
                        keybindRightClick("updater.backwards_stepping");

                        ImGui::TableNextColumn();
                        if (slui::drag("Steps",
                                        GrapeEngine::get()
                                            ->practiceFix()
                                            .m_maxStoredFrames->inner(),
                                        1u, 2400u)
                                .changed) {
                            GrapeEngine::get()
                                ->practiceFix()
                                .m_maxStoredFrames->notifyChange();
                        }
                    ImGui::EndTable();
                }

                slui::divider();

                slui::text("Autoclicker", m_medium);

                if (skeetMenu) {
                    slui::checkbox("Enabled##Autoclicker",
                                   GrapeEngine::get()->autoclicker().m_enabled->inner());
                    keybindRightClick("autoclicker.enabled");
                    slui::dropdown(
                        customMenu ? "##AutoclickerPlayer"
                                   : "Player##Autoclicker",
                        m_state.m_autoclickerState,
                        *reinterpret_cast<int*>(
                            &GrapeEngine::get()->autoclicker().m_player),
                        popupShaderFn);
                } else if (ImGui::BeginTable(
                               "AutoclickerRow", 2,
                               ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Enabled##Autoclicker",
                                   GrapeEngine::get()->autoclicker().m_enabled->inner());
                    keybindRightClick("autoclicker.enabled");
                    ImGui::TableNextColumn();
                    slui::dropdown(
                        customMenu ? "##AutoclickerPlayer"
                                   : "Player##Autoclicker",
                        m_state.m_autoclickerState,
                        *reinterpret_cast<int*>(
                            &GrapeEngine::get()->autoclicker().m_player), popupShaderFn);
                    ImGui::EndTable();
                }
                auto& autoclicker = GrapeEngine::get()->autoclicker();
                const bool linkHold =
                    autoclicker.m_holdFrames == autoclicker.m_releaseFrames;
                const auto hold = slui::drag(
                    "Hold Frames##Autoclicker", autoclicker.m_holdFrames, 1,
                    std::numeric_limits<int>::max(), 1.0f, "{} Frame(s)");
                if (linkHold && hold.changed &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    autoclicker.m_releaseFrames = autoclicker.m_holdFrames;
                const bool linkRelease =
                    autoclicker.m_holdFrames == autoclicker.m_releaseFrames;
                const auto release = slui::drag(
                    "Release Frames##Autoclicker",
                    autoclicker.m_releaseFrames, 1,
                    std::numeric_limits<int>::max(), 1.0f, "{} Frame(s)");
                if (linkRelease && release.changed &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    autoclicker.m_holdFrames = autoclicker.m_releaseFrames;
                slui::checkbox("Swift Clicks",
                                GrapeEngine::get()->autoclicker().m_performSwifts);
                slui::checkbox("Moving Gap Assist",
                                GrapeEngine::get()->autoclicker().m_movingGap);
                slui::drag(
                    "Safety Frames##Autoclicker",
                    GrapeEngine::get()->autoclicker().m_movingGapLookahead,
                    1, 30, 1.0f, "{} Frame(s)");
                autoclicker.saveSettings();
                keybindRightClick("autoclicker.swift_clicks");

                slui::divider();

                slui::text("Other", m_medium);

                slui::checkbox("Mirror Inputs",
                                GrapeEngine::get()->macro().m_mirrorInputs);
                keybindRightClick("replay.mirror_inputs");
                slui::checkbox("Mirror Inverted",
                                GrapeEngine::get()->macro().m_mirrorInverted);
                keybindRightClick("replay.mirror_inverted");

                slui::checkbox("Maintain Gravity",
                                GrapeEngine::get()->macro().m_maintainGravity);
                keybindRightClick("replay.maintain_gravity");

                slui::checkbox("No Mirror Portals",
                                GrapeEngine::get()->timeline().m_noMirror->inner());
                keybindRightClick("updater.no_mirror");
            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Prediction, [&]() {
                if (!skeetMenu && !customMenu)
                    slui::text("Prediction", m_bold);

                slui::divider(false);

                slui::text("Automation", m_medium);

                slui::checkbox("Prevent Death",
                                GrapeEngine::get()->timeline().m_preventDeath->inner());
                keybindRightClick("updater.prevent_death");
                slui::checkbox(
                    "Use Trajectory##PD",
                    GrapeEngine::get()->timeline().m_fullGamePrediction->inner());
                keybindRightClick("updater.full_game_prediction");
                
                slui::divider();

                slui::text("Simulation", m_medium);

                if (slui::button("Find Best Frame").pressed) {
                    GrapeEngine::get()->timeline().findBestFrameCandidate();
                }

                slui::drag(
                    "Threshold##Prediction",
                    GrapeEngine::get()->timeline().m_acceptablePrediction->inner(), 0.0f,
                    1.0f, 0.01f, "{:.2f}");

                slui::divider();

                slui::text("Pathfinder", m_medium);

                auto& pathfinder = GrapeEngine::get()->pathfinder();
                slui::checkbox("Pathfinder", pathfinder.m_enabled->inner());
                keybindRightClick("pathfinder.enabled");
                slui::drag("Stuck Deaths##Pathfinder",
                           pathfinder.m_stuckDeaths, 1, 50, 1.0f, "{}");
                if (!GrapeEngine::get()->timeline().m_backwardsStepping->inner())
                    slui::text(
                        "Enable Backwards Stepping for checkpoint learning.");
                slui::divider();

                slui::text("Click Timing", m_medium);

                slui::checkbox("Proportional Timing", pathfinder.m_proportional);
                if (pathfinder.m_proportional) {
                    slui::triple_slider("Hold / Release / Swift##pf",
                                        pathfinder.m_holdPct,
                                        pathfinder.m_releasePct,
                                        pathfinder.m_swiftPct);
                    slui::drag("Cycle Frames##pf", pathfinder.m_cycleFrames,
                               2, 120,
                               1.0f, "{} Frame(s)");
                }
                pathfinder.saveSettings();

            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Edit, [&]() {
                if (!skeetMenu && !customMenu) slui::text("Edit", m_bold);

                if (customMenu)
                    slui::Config::get().customSectionHeight = std::max(
                        1.0f, ImGui::GetContentRegionAvail().y -
                            ImGui::GetFontSize() - 12.0f);
                slui::divider(false);
                if (customMenu) slui::text("Input Editor", m_medium);

                auto& replay = GrapeEngine::get()->macro().m_actionAtom;
                auto& inputs = replay.m_actions;
                int inputIndex = GrapeEngine::get()->macro().getInputIndex();

                if (inputs.empty()) {
                    slui::text("No replay loaded.");
                    m_state.m_editIndex = 0;
                    m_state.m_editSelectionInitialized = false;
                } else {
                    m_state.m_editIndex = std::clamp(
                        m_state.m_editIndex, 0, static_cast<int>(inputs.size()) - 1);
                    if (!m_state.m_editSelectionInitialized) {
                        m_state.m_editIndex = std::clamp(
                            inputIndex, 0, static_cast<int>(inputs.size()) - 1);
                        m_state.m_editSelectionInitialized = true;
                    }
                }

                const float editorHeight = skeetMenu
                    ? 440.0f : ImGui::GetContentRegionAvail().y;
                const float buttonHeight =
                    (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y +
                     ImGui::GetStyle().CellPadding.y * 2.0f) *
                    3.0f;
                const auto drawEditor = [&] {
                    const float top = ImGui::GetCursorPosY();
                    if (!inputs.empty()) {
                        const int i = m_state.m_editIndex;
                        auto& input = inputs[i];
                        if (slui::drag("Frame##SelectedInput", input.m_frame,
                                uint64_t{0},
                                std::numeric_limits<uint64_t>::max()).changed) {
                            input.recalculateDelta(
                                i == 0 ? 0 : inputs[i - 1].m_frame);
                            if (i + 1 < static_cast<int>(inputs.size()))
                                inputs[i + 1].recalculateDelta(input.m_frame);
                            GrapeEngine::get()->timeline().m_tps->notifyChange();
                        }
                        if (slui::checkbox("Holding / Click", input.m_holding).pressed)
                            GrapeEngine::get()->timeline().m_tps->notifyChange();
                        if (slui::checkbox("Player 2", input.m_player2).pressed)
                            GrapeEngine::get()->timeline().m_tps->notifyChange();
                        using ActionType = slc::v3::Action::ActionType;
                        bool left = input.m_type == ActionType::Left;
                        if (slui::checkbox("Left", left).pressed) {
                            input.m_type = left ? ActionType::Left
                                                : ActionType::Jump;
                            GrapeEngine::get()->timeline().m_tps->notifyChange();
                        }
                        bool right = input.m_type == ActionType::Right;
                        if (slui::checkbox("Right", right).pressed) {
                            input.m_type = right ? ActionType::Right
                                                 : ActionType::Jump;
                            GrapeEngine::get()->timeline().m_tps->notifyChange();
                        }
                    }
                    ImGui::SetCursorPosY(std::max(
                        ImGui::GetCursorPosY(), top + editorHeight - buttonHeight));
                    if (!ImGui::BeginTable(
                            "MacroButtons", 2,
                            ImGuiTableFlags_SizingStretchSame,
                            skeetMenu ? ImVec2(220.0f, 0.0f) : ImVec2()))
                        return;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (slui::raw_button(
                            "Add Input", ImVec2(-FLT_MIN, 0.0f))) {
                        if (inputs.empty()) {
                            inputs.emplace_back(0, 0,
                                slc::v3::Action::ActionType::Jump, true, false);
                            m_state.m_editIndex = 0;
                        } else {
                            int i = m_state.m_editIndex;
                            const auto type = inputs[i].isPlayer()
                                ? inputs[i].m_type
                                : slc::v3::Action::ActionType::Jump;
                            auto next = slc::v3::Action(inputs[i].m_frame, 0,
                                type,
                                !inputs[i].m_holding, inputs[i].m_player2);
                            inputs.insert(inputs.begin() + i + 1, next);
                            m_state.m_editIndex = i + 1;
                            inputs[i + 1].recalculateDelta(inputs[i].m_frame);
                            if (i + 2 < static_cast<int>(inputs.size()))
                                inputs[i + 2].recalculateDelta(inputs[i + 1].m_frame);
                        }
                    }
                    ImGui::TableNextColumn();
                    if (slui::raw_button(
                            "Delete Input", ImVec2(-FLT_MIN, 0.0f)) &&
                        !inputs.empty()) {
                        int i = m_state.m_editIndex;
                        inputs.erase(inputs.begin() + i);
                        if (i < static_cast<int>(inputs.size()))
                            inputs[i].recalculateDelta(i == 0 ? 0 : inputs[i - 1].m_frame);
                        m_state.m_editIndex = std::min(
                            i, static_cast<int>(inputs.size()) - 1);
                        if (m_state.m_editIndex < 0) m_state.m_editIndex = 0;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (slui::raw_button(
                            "Remove P1 Inputs", ImVec2(-FLT_MIN, 0.0f))) {
                        inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [](auto& i) { return !i.m_player2; }), inputs.end());
                        for (size_t j = 0; j < inputs.size(); ++j) inputs[j].recalculateDelta(j == 0 ? 0 : inputs[j - 1].m_frame);
                        m_state.m_editIndex = 0;
                    }
                    ImGui::TableNextColumn();
                    if (slui::raw_button(
                            "Remove P2 Inputs", ImVec2(-FLT_MIN, 0.0f))) {
                        inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [](auto& i) { return i.m_player2; }), inputs.end());
                        for (size_t j = 0; j < inputs.size(); ++j) inputs[j].recalculateDelta(j == 0 ? 0 : inputs[j - 1].m_frame);
                        m_state.m_editIndex = 0;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (slui::raw_button(
                            "Flip Hold & Release", ImVec2(-FLT_MIN, 0.0f))) {
                        for (auto& i : inputs) i.m_holding = !i.m_holding;
                    }
                    ImGui::TableNextColumn();
                    if (slui::raw_button(
                            "Flip P1 & P2", ImVec2(-FLT_MIN, 0.0f))) {
                        for (auto& i : inputs) i.m_player2 = !i.m_player2;
                    }

                    ImGui::EndTable();
                };
                const auto drawInputs = [&] {
                    ImGui::BeginChild(
                        "InputList", ImVec2(0.0f, editorHeight), !customMenu);
                    for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
                        const auto& input = inputs[i];
                        using ActionType = slc::v3::Action::ActionType;
                        const char* action = input.m_type == ActionType::Left
                            ? "Left" : input.m_type == ActionType::Right
                            ? "Right" : "";
                        const std::string label = action[0]
                            ? fmt::format("{} {} at {}##Input{}", action,
                                  input.m_holding ? "Click" : "Release",
                                  input.m_frame, i)
                            : fmt::format("{} at {}##Input{}",
                                  input.m_holding ? "Click" : "Release",
                                  input.m_frame, i);
                        if (ImGui::Selectable(
                                label.c_str(), m_state.m_editIndex == i))
                            m_state.m_editIndex = i;
                        if (i == inputIndex) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndChild();
                };

                if (customMenu) {
                    drawEditor();
                    slui::divider();
                    slui::text("Inputs", m_medium);
                    drawInputs();
                } else if (skeetMenu) {
                    slui::hide_section_box();
                    drawEditor();
                    slui::divider();
                    slui::hide_section_box();
                    drawInputs();
                } else if (ImGui::BeginTable(
                               "MacroEditorLayout", 2,
                               ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    drawEditor();
                    ImGui::TableNextColumn();
                    drawInputs();
                    ImGui::EndTable();
                }
            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Render, [&]() {
                if (!skeetMenu && !customMenu) slui::text("Render", m_bold);

                slui::divider(false);
                if (customMenu) slui::text("Render", m_medium);

                auto renderer = Renderer::get();

                if (!renderer->isFFmpegLoaded()) {
                    slui::text("FFmpeg not loaded.");

                    if (m_ffmpegDownloadProgress < 0.0 &&
                        slui::button("Download").pressed) {
                        m_ffmpegDownloadProgress = 0.0;
                        geode::log::info("Downloading FFmpeg...");
                        auto req = web::WebRequest();

                        req.onProgress([&](const web::WebProgress& prog) {
                            if (prog.downloadTotal() == 0) {
                                return;
                            }

                            m_ffmpegDownloadProgress =
                                static_cast<double>(prog.downloaded()) /
                                static_cast<double>(prog.downloadTotal());
                        });

                        m_webListener.spawn(
                            req.get(ffmpegUrl), [&](web::WebResponse resp) {
                                const auto data = resp.data();

                                geode::log::info("Verifying checksum...");
                                uint64_t hash = fnv1aHash(data);
                                constexpr uint64_t EXPECTED =
                                    0x44618c661fa11607ull;
                                if (hash != EXPECTED) {
                                    geode::log::error(
                                        "Invalid checksum! Aborting FFmpeg "
                                        "loader");
                                    m_ffmpegDownloadProgress = -1.0;
                                    return;
                                }

                                geode::log::info(
                                    "Checksum valid! Unzipping...");
                                auto unzipResult =
                                    geode::utils::file::Unzip::create(data);
                                if (unzipResult.isErr()) {
                                    return;
                                }

                                auto unzip = std::move(unzipResult.unwrap());
                                auto ffmpegDir =
                                    Mod::get()->getTempDir() / "ffmpeg";
                                if (unzip.extractAllTo(ffmpegDir).isErr()) {
                                    return;
                                }

                                namespace fs = std::filesystem;

                                auto libDir = grape::paths::directory("libraries");
                                geode::log::info(
                                    "Copying dlls from temp dir `{}`...",
                                    ffmpegDir);
                                for (const auto& entry :
                                     fs::directory_iterator(ffmpegDir)) {
                                    if (entry.path().extension() == ".dll") {
                                        fs::copy_file(
                                            entry,
                                            libDir / entry.path().filename(),
                                            fs::copy_options::
                                                overwrite_existing);
                                    }
                                }

                                fs::remove_all(ffmpegDir);

                                Renderer::get()->loadFFmpeg();
                                m_ffmpegDownloadProgress = -1.0;
                            });
                    }

                    if (m_ffmpegDownloadProgress >= 0.95) {
                        slui::text(
                            "Loading FFmpeg! Please do not close your game...");
                    } else if (m_ffmpegDownloadProgress >= 0.0) {
                        slui::text(
                            fmt::format("Downloading FFmpeg... {:.1f}%",
                                        m_ffmpegDownloadProgress * 100.0));
                    }

                    return;
                }

                if (renderer->m_autoVideoName->inner()) {
                    slui::input_text(
                        "Video Template", "Template",
                        Renderer::get()->m_videoNameTemplate->inner());
                } else {
                    slui::input_text("Video Name", "Video",
                                      Renderer::get()->m_settings.m_outputPath);
                }

                slui::checkbox("Auto Video Name",
                                renderer->m_autoVideoName->inner());
                keybindRightClick("renderer.auto_video_name");

                if (renderer->isRecording()) {
                    if (slui::button("Stop").pressed) {
                        renderer->signalStop();
                    }
                } else {
                    if (!PlayLayer::get() && !renderer->m_shouldStart) {
                        if (slui::button("Start").pressed) {
                            renderer->queueStart();
                        }
                    } else if (renderer->m_shouldStart) {
                        slui::text("Waiting to enter level...");
                    } else if (PlayLayer::get()) {
                        if (slui::button("Start Here").pressed) {
                            (void)renderer->start();
                        }
                    }
                }

                auto ar = AudioRecorder::get();
                slui::checkbox("Audio Preview", ar->m_audioPreview->inner());
                keybindRightClick("audio.preview");

                slui::checkbox("Show Labels While Recording",
                                GrapeSettings::get()->renderLabelsWhileRecording);
                keybindRightClick("render.labels_while_recording");

                slui::divider();

                slui::text("Presets", m_medium);

                const auto presetNameInput = [&] {
                    m_state.m_presetAutocomplete.suggestions =
                        filterCandidates(m_state.m_presetNames,
                                         m_state.m_presetName);
                    slui::input_text_autocomplete(
                        "##PresetName", "Preset Name", m_state.m_presetName,
                        m_state.m_presetAutocomplete, [&]() {
                            renderBlurBg(12.0f, 1.5f, m_state.m_useShader->inner());
                        });
                };
                bool loadPreset = false;
                bool savePreset = false;
                if (skeetMenu) {
                    slui::next_input_full_width();
                    presetNameInput();
                    const auto actions = drawSkeetActionRow(
                        "RenderPresets", "Load##Preset", "Save##Preset");
                    loadPreset = actions.first;
                    savePreset = actions.second;
                } else if (ImGui::BeginTable(
                               "RenderPresets", 3, ImGuiTableFlags_None)) {
                    ImGui::TableSetupColumn(
                        "Input", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(
                        "Load", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn(
                        "Save", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    presetNameInput();
                    ImGui::TableNextColumn();
                    loadPreset = slui::raw_button("Load##Preset");
                    ImGui::TableNextColumn();
                    savePreset = slui::raw_button("Save##Preset");
                    ImGui::EndTable();
                }
                auto presetPath =
                    grape::paths::directory("presets") /
                    (m_state.m_presetName + ".json");
                if (loadPreset) {
                    if (std::filesystem::exists(presetPath)) {
                        renderer->loadSettings(presetPath);
                    } else {
                        geode::log::error(
                            "Preset file does not exist: {}",
                            presetPath.string());
                    }
                }
                if (savePreset) renderer->saveSettings(presetPath);

                if (customMenu)
                    slui::Config::get().customSectionIndex |= 1;
                slui::divider();

                slui::text("Video Settings", m_medium);

                slui::fraction(
                    2.0,
                    [&]() {
                        slui::drag("Width", renderer->m_settings.m_width, 1,
                                    10000, 1.0f);
                        slui::spacer(16.0);
                        slui::drag("Height", renderer->m_settings.m_height, 1,
                                    10000, 1.0f);

                        slui::drag("FPS", renderer->m_settings.m_fps, 1, 1000,
                                    1.0f);
                        slui::spacer(16.0);
                        if (slui::drag("Bitrate", m_state.m_bitrate, 0.0,
                                        10000.0, 1.0f, "{:g}Mbps")
                                .changed) {
                            renderer->m_settings.m_bitrate =
                                std::round(m_state.m_bitrate * 1'000'000.0);
                        }
                    },
                    16.0);

                slui::checkbox("Force Codec", renderer->m_settings.m_forceCodec);
                if (renderer->m_settings.m_forceCodec) {
                    slui::input_text("Codec", "Codec",
                                      renderer->m_settings.m_codec);
                } else {
                    if (slui::button(m_state.m_encodersDetected
                                         ? "Re-detect Encoders"
                                         : "Detect Encoders")
                            .pressed) {
                        auto encoders = renderer->detectEncoders();
                        m_state.m_encoderState.options = encoders;
                        m_state.m_encodersDetected = true;

                        // Keep the current selection pointing at the active
                        // codec if it's still in the detected list.
                        int selected = 0;
                        for (int i = 0;
                             i < static_cast<int>(encoders.size()); ++i) {
                            if (encoders[i] == renderer->m_settings.m_codec) {
                                selected = i;
                                break;
                            }
                        }
                        m_state.m_encoderState.selectedIndex = selected;
                        m_state.m_codec = selected;
                        if (!encoders.empty()) {
                            renderer->m_settings.m_codec = encoders[selected];
                        }
                    }

                    if (m_state.m_encoderState.options.empty()) {
                        slui::text(
                            m_state.m_encodersDetected
                                ? "No supported encoders found - enable Force "
                                  "Codec to type one manually."
                                : "Run detection to list encoders your PC "
                                  "supports.");
                    } else {
                        if (slui::dropdown("Codec", m_state.m_encoderState,
                                           m_state.m_codec, popupShaderFn)
                                .changed) {
                            m_state.m_codec = std::clamp(
                                m_state.m_codec, 0,
                                static_cast<int>(
                                    m_state.m_encoderState.options.size()) - 1);
                            renderer->m_settings.m_codec =
                                m_state.m_encoderState.options[m_state.m_codec];
                        }
                    }
                }
                slui::input_text("Extension", "Extension",
                                  renderer->m_settings.m_extension);
                slui::drag("After End Time",
                            renderer->m_settings.m_afterEndTime, 0.0f, 10000.0f,
                            1.0f, "{:.2f}s");
                slui::checkbox("SSB Fix",
                                GrapeEngine::get()->timeline().m_ssbFix->inner());

                slui::drag("Music Volume", renderer->m_settings.m_musicVolume,
                            0.0, 1.0, 0.01, "{:.2f}");
                slui::drag("SFX Volume", renderer->m_settings.m_sfxVolume, 0.0,
                            1.0, 0.01, "{:.2f}");
                slui::checkbox("Record 1st Attempt Pause",
                                renderer->m_settings.m_firstAttemptPause);
                keybindRightClick("render.first_attempt_pause");

                slui::input_text("FFmpeg Args", "-preset slow ...",
                                  renderer->m_settings.m_renderArgs);
            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Settings, [&]() {
                if (!skeetMenu && !customMenu) slui::text("Settings", m_bold);

                slui::divider(false);

                slui::text("Interface", m_medium);

#ifdef GRAPE_PRIVATE_PC
                int menuStyle = savedMenuStyle == 1 ? 2
                    : savedMenuStyle == 2 ? 1 : 0;
                static slui::DropdownState menuStyleState{
                    {"Default", "imGui", "Skeet.cc"}, 0};
                menuStyleState.selectedIndex = menuStyle;
                if (slui::dropdown(
                        "Menu Style", menuStyleState, menuStyle).changed) {
                    s_pendingMenuStyle = menuStyle == 2 ? 1
                        : menuStyle == 1 ? 2 : 0;
                    m_state.m_visible->inner() = false;
                }
#endif

                if (slui::button("Open Grape Folder").pressed) {
                    geode::utils::file::openFolder(
                        grape::paths::dataRoot());
                }

                if (slui::drag("UI Scale", m_state.m_uiScale->inner(), 0.1f,
                                10.0f, 0.01f, "{:.2f}x")
                        .changed) {
                    m_state.m_uiScale->notifyChange();
                }

                if (m_state.m_restartGameInfo) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                    slui::text(
                        "It's recommended to restart the game after "
                        "changing "
                        "UI scale.");
                    ImGui::PopStyleColor();
                }

                if (slui::drag("UI Opacity", m_state.m_opacity->inner(), 0.1f,
                                1.0f, 0.01f, "{:.2f}")
                        .changed) {
                    m_state.m_opacity->notifyChange();
                }

                slui::divider();

                drawImGuiThemeEditor(m_bold);

                slui::divider();

                slui::text("Backups", m_medium);

                slui::checkbox(
                    "Auto-Save At Level End",
                    GrapeEngine::get()->macro().m_autosaveAtLevelEnd->inner());
                keybindRightClick("replay.autosave_at_level_end");

                auto& rs = GrapeEngine::get()->macro();
                if (slui::checkbox("Auto-Backup",
                                    GrapeEngine::get()
                                        ->macro()
                                        .m_autosaveAtInterval->inner())
                        .pressed) {
                    rs.m_autosaveAtInterval->notifyChange();
                }

                if (rs.m_autosaveAtInterval->inner()) {
                    if (slui::drag("Interval",
                                    rs.m_autosaveInterval->inner(), 1.0, 3600.0,
                                    1.0, "{:.0f}s")
                            .changed) {
                        rs.m_autosaveInterval->notifyChange();
                    }
                }

                slui::divider();

                slui::text("Labels", m_medium);

                slui::checkbox("Display Labels",
                                GrapeEngine::get()->labels().m_globalEnabled);
                keybindRightClick("labels.global_enabled");

                slui::dropdown("Label##Selector", m_state.m_labelState,
                                m_state.m_labelState.selectedIndex, [&]() {
                                    renderBlurBg(12.0f, 1.5f,
                                                 m_state.m_useShader->inner());
                                });

                auto& label = GrapeEngine::get()
                                  ->labels()
                                  .m_labels[m_state.m_labelState.selectedIndex]
                                  .m_config;

                if (label.m_customFont.empty()) {
                    m_state.m_labelFontsState.selectedIndex =
                        (label.m_font == Label::LabelFont::BigFont) ? 0 : 1;
                } else {
                    auto found = std::find(
                        m_state.m_customFontFiles.begin(),
                        m_state.m_customFontFiles.end(), label.m_customFont);
                    m_state.m_labelFontsState.selectedIndex = found ==
                            m_state.m_customFontFiles.end()
                        ? 1
                        : static_cast<int>(found -
                              m_state.m_customFontFiles.begin()) + 2;
                }

                if (slui::checkbox("Enabled##Label", label.m_enabled)
                        .pressed) {
                    bot->labels().m_requiresRefresh = true;
                }

                if (slui::drag("Opacity##Label", label.m_opacity, 0.0f, 1.0f,
                                0.01f, "{:.2f}x")
                        .changed) {
                    bot->labels().m_requiresRefresh = true;
                }

                if (slui::drag("Size##Label", label.m_scale, 0.0f, 100.0f,
                                1.0f, "{:.2f}x")
                        .changed) {
                    bot->labels().m_requiresRefresh = true;
                }

                if (slui::dropdown("Font##Label", m_state.m_labelFontsState,
                                    m_state.m_labelFontsState.selectedIndex,
                                    [&]() {
                                        renderBlurBg(
                                            12.0f, 1.5f,
                                            m_state.m_useShader->inner());
                                    })
                        .changed) {
                    int idx = m_state.m_labelFontsState.selectedIndex;
                    if (idx < 2) {
                        label.m_customFont.clear();
                        label.m_font = (idx == 0)
                            ? Label::LabelFont::BigFont
                            : Label::LabelFont::ChatFont;
                    } else if (idx - 2 < static_cast<int>(
                                   m_state.m_customFontFiles.size())) {
                        label.m_customFont =
                            m_state.m_customFontFiles[idx - 2];
                    }
                    bot->labels().m_requiresRefresh = true;
                }

                if (slui::dropdown(
                        "Position##Label", m_state.m_labelPositionsState,
                        *reinterpret_cast<int*>(&label.m_anchor),
                        [&]() {
                            renderBlurBg(12.0f, 1.5f,
                                         m_state.m_useShader->inner());
                        })
                        .changed) {
                    bot->labels().m_requiresRefresh = true;
                }

                slui::divider();

                slui::text("Updater", m_medium);

                if (ImGui::BeginTable(
                        "LockDelta", 2,
                        ImGuiTableFlags_SizingStretchProp,
                        skeetMenu ? ImVec2(220.0f, 0.0f) : ImVec2())) {
                    // Weighted rather than an even split: with equal columns
                    // the dropdown started too far right and clipped its widest
                    // option to "Performan". Giving it the larger share moves
                    // its left edge over and fits the full text.
                    ImGui::TableSetupColumn("##lockDeltaToggle",
                                            ImGuiTableColumnFlags_WidthStretch,
                                            0.75f);
                    ImGui::TableSetupColumn("##lockDeltaMode",
                                            ImGuiTableColumnFlags_WidthStretch,
                                            1.25f);
                    ImGui::TableNextColumn();
                    slui::checkbox("Lock Delta",
                                    bot->timeline().m_lockDelta->inner());
                    keybindRightClick("updater.lock_delta");
                    ImGui::TableNextColumn();
                    slui::dropdown("##LockDeltaMode", m_state.m_lockDeltaState,
                        bot->timeline().m_lockDeltaMode->inner(), [&]() {
                            renderBlurBg(12.0f, 1.5f,
                                         m_state.m_useShader->inner());
                        });
                    ImGui::EndTable();
                }

                slui::checkbox("Real Time",
                                bot->timeline().m_realTime->inner());
                keybindRightClick("updater.real_time");

                if (!bot->timeline().m_realTime->inner()) {
                    slui::checkbox("Dynamic UPR",
                                    bot->timeline().m_dynamicUpr->inner());

                    if (bot->timeline().m_dynamicUpr->inner()) {
                        slui::drag("Target FPS",
                                    bot->timeline().m_fpsTarget->inner(), 1.0,
                                    480.0, 1.0f, "{:.0f} FPS");
                    } else {
                        slui::drag("Max UPR", bot->timeline().m_maxUPR->inner(),
                                    1u, 1000000u, 1.0f);

                        slui::checkbox(
                            "Use Visual Updates",
                            bot->timeline().m_useVisualUpdates->inner());
                    }
                }

                slui::divider();

                slui::text("Miscellaneous", m_medium);

                if (slui::button("Disable Bot").pressed) {
                    GrapeEngine::get()->m_enabled->inner() = false;
                    GrapeEngine::get()->m_enabled->notifyChange();
                }

            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Scripts, [&]() {
#ifdef GRAPE_PRIVATE_PC
                auto& scripts = grape::pc::ScriptEngine::get();
                static std::string selectedScript;
                slui::divider(false);
                slui::text("Lua Scripts", m_bold);
                if (slui::button("Open Scripts Folder").pressed)
                    geode::utils::file::openFolder(grape::paths::directory("scripts"));
                if (slui::button("Refresh").pressed) scripts.refresh();
                auto available = scripts.scripts();
                const auto selectedExists = std::ranges::find(
                    available, selectedScript, &grape::pc::ScriptStatus::name);
                if (selectedExists == available.end())
                    selectedScript = available.empty() ? "" : available.front().name;
                slui::DropdownState selector;
                for (const auto& script : available)
                    selector.options.push_back(script.name);
                int selectedIndex = -1;
                for (int i = 0; i < static_cast<int>(selector.options.size()); ++i)
                    if (selector.options[i] == selectedScript) selectedIndex = i;
                if (slui::dropdown("Script", selector, selectedIndex).changed &&
                    selectedIndex >= 0) {
                    selectedScript = selector.options[selectedIndex];
                    scripts.load(selectedScript);
                    available = scripts.scripts();
                }
                const auto current = std::ranges::find(
                    available, selectedScript, &grape::pc::ScriptStatus::name);
                if (current != available.end()) {
                    slui::text(current->status);
                    if (current->loaded) {
                        if (ImGui::BeginTable("ScriptActionsRow", 2,
                                              ImGuiTableFlags_SizingStretchSame,
                                              skeetMenu ? ImVec2(220.0f, 0.0f)
                                                        : ImVec2())) {
                            ImGui::TableNextColumn();
                            if (slui::raw_button("Unload",
                                                 ImVec2(-FLT_MIN, 0.0f)))
                                scripts.unload(selectedScript);
                            ImGui::TableNextColumn();
                            if (slui::raw_button("Reload",
                                                 ImVec2(-FLT_MIN, 0.0f)))
                                scripts.load(selectedScript);
                            ImGui::EndTable();
                        }
                    } else if (slui::button("Load").pressed) {
                        scripts.load(selectedScript);
                    }
                } else {
                    slui::text("No Lua scripts found");
                }
                slui::divider();
                slui::text("Script Menu", m_medium);
                scripts.draw();
#endif
            });
        }
        ImGui::EndChild();
        if (skeetMenu) ImGui::EndGroup();

        if (!skeetMenu) {
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetWindowPos(),
                ImGui::GetWindowPos() + ImGui::GetWindowSize(),
                ImGui::GetColorU32(ImVec4(1.0, 1.0, 1.0, 0.1f)),
                // Same reason as the fill above: a square outline poked past
                // the rounded corners.
                ImGui::GetStyle().WindowRounding,
                ImDrawFlags_RoundCornersAll,
                1.0f * slui::Config::get().uiScale);
        }

        slui::off_the_screen();  
        slui::text(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
            "!@#$"
            "%^&*()_+[]{}|;':\",./<>?`~=-");
        slui::text(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
            "!@#$"
            "%^&*()_+[]{}|;':\",./<>?`~=-",
            m_bold);
        slui::text(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
            "!@#$"
            "%^&*()_+[]{}|;':\",./<>?`~=-",
            m_medium);

        slui::text(
            "\uf192    \uefba    \uf03d    \uf121    \uf013    \uf078    "
            "\uf054    \uf00c    \uf044    \uf51b    \uf004    \uf05f    "
            "\uf01f");
        slui::text(
            "\uf192    \uefba    \uf03d    \uf121    \uf013    \uf078    "
            "\uf054    \uf00c    \uf044    \uf51b    \uf004    \uf05f    "
            "\uf01f",
            m_medium);
    });

    drawKeybindContextMenu();

#ifdef GRAPE_PRIVATE_PC
    if (customMenu) popDefaultMenuStyle();
    else if (skeetMenu) grape::pc::popSkeetStyle();
#endif
    slui::Config::get().customMode = false;
    slui::Config::get().skeetMode = false;

#ifdef SILICATE_PROTECT
    VMProtectEnd();
#endif
}
