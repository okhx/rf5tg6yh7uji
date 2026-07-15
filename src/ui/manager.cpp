 #include "manager.hpp"

#ifdef GEODE_IS_WINDOWS
#include <winuser.h>
#else
// Keep saved Windows keybinds readable on platforms where winuser.h is not
// available. These are the documented Windows virtual-key values.
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
#include <slc/formats/v3/atom.hpp>
#include <slc/formats/v3/replay.hpp>
#include <variant>
#include <fstream>

#include "Geode/utils/string.hpp"
#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "checkpoint/fix.hpp"
#include "hook.hpp"
#include "label/label.hpp"
#include "render/dsp.hpp"
#include "render/renderer.hpp"
#include "replay/system.hpp"
#include "settings/settings.hpp"
#include "shared/keys.hpp"
#include "trajectory/trajectory.hpp"

#include "imgui.h"
#include "util/paths.hpp"
#include "imgui_helpers.hpp"
#include "lib/stb_image.h"

#ifdef SILICATE_PROTECT
#include "VMProtect/VMProtectSDK.h"
#endif

using namespace geode::prelude;

UIManager::UIManager() : m_font(nullptr), m_medium(nullptr), m_bold(nullptr) {}

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

        // Adapted from https://www.shadertoy.com/view/4st3DM
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

            // fragColor -= vec4(
            //     sin(eh*0.7+(y+n*1.2)*5.-n*5.),
            //     sin(eh*0.7+(y+n*1.2)*5.-n*5.),
            //     sin(eh*0.7+(y+n*1.2)*5.-n*5.),
            // 1.0) * phase * vec4(0.2);
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

static void drawImGuiThemeEditor(ImFont* headingFont) {
    static char name[128] = "custom";
    slui::text("Theme", headingFont);
    ImGui::Separator();
    std::string safeName(name);
    safeName.erase(std::remove_if(safeName.begin(), safeName.end(), [](char c) {
        return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
               c == '\"' || c == '<' || c == '>' || c == '|';
    }), safeName.end());
    const auto path = silicate::paths::directory("themes") / (safeName + ".theme");
    if (ImGui::BeginTable("ThemeSaveLoad", 4, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Load", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##ThemeName", "Theme name", name, sizeof(name));
        ImGui::TableNextColumn();
        if (ImGui::Button("Save")) saveImGuiTheme(path);
        ImGui::TableNextColumn();
        if (ImGui::Button("Load")) loadImGuiTheme(path);
        ImGui::TableNextColumn();
        if (ImGui::Button("Folder"))
            geode::utils::file::openFolder(silicate::paths::directory("themes"));
        ImGui::EndTable();
    }
    ImGui::Checkbox("Docked", &SLSettings::get()->fitMenuToContent);

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

    ImGui::GetIO().Fonts->Build();

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

    ImGui::GetIO().Fonts->Build();

    m_font   = mainFont;
    m_medium = mediumFont;

    m_bold = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        geode::utils::string::pathToString(
            Mod::get()->getResourcesDir() / "font_bold.ttf").c_str(), 32.0f);

    {
        namespace fs = std::filesystem;
        auto fontsDir = silicate::paths::directory("fonts");
        fs::create_directories(fontsDir);

        m_state.m_customFontNames.clear();
        m_state.m_customFonts.clear();

        auto* io = &ImGui::GetIO();
        for (const auto& entry : fs::directory_iterator(fontsDir)) {
            auto ext = entry.path().extension().string();
            if (ext != ".ttf" && ext != ".otf") continue;

            std::string name = entry.path().stem().string();
            std::string path = entry.path().string();

            ImFont* font = io->Fonts->AddFontFromFileTTF(path.c_str(), 20.0f);
            if (font) {
                m_state.m_customFontNames.push_back(name);
                m_state.m_customFonts.push_back(font);
            }
        }

        if (!m_state.m_customFontNames.empty()) {
            io->Fonts->Build();
        }

        m_state.m_labelFontsState.options = {"Big", "Regular"};
    }

    m_state.m_rainbow->notifyChange();
    m_state.m_playAnimations->notifyChange();
    m_state.m_animationSpeed->notifyChange();

    for (const auto& label : Bot::get()->labels().m_labels) {
        m_state.m_labelState.options.push_back(label.getFriendlyName().c_str());
    }

    m_state.m_replayNames.clear();
    for (const auto& entry : std::filesystem::directory_iterator(
              silicate::paths::directory("replays"))) {
        if (entry.is_regular_file() &&
            (entry.path().extension() == ".grape" ||
             entry.path().extension() == ".slc")) {
            m_state.m_replayNames.push_back(entry.path().stem().string());
        }
    }

    m_state.m_presetNames.clear();
    for (const auto& entry : std::filesystem::directory_iterator(
              silicate::paths::directory("presets"))) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            m_state.m_presetNames.push_back(entry.path().stem().string());
        }
    }

    m_state.m_scriptNames.clear();
    for (const auto& entry : std::filesystem::directory_iterator(
              silicate::paths::directory("scripts"))) {
        if (entry.is_regular_file() && entry.path().extension() == ".lua") {
            m_state.m_scriptNames.push_back(entry.path().stem().string());
        }
    }

    m_replayAutocomplete.suggestions = m_state.m_replayNames;
    m_state.m_presetAutocomplete.suggestions = m_state.m_presetNames;
    m_state.m_scriptAutocomplete.suggestions = m_state.m_scriptNames;

    m_state.m_bgColorState.colors = SLSettings::get()->layoutBgColor;
    m_state.m_groundColorState.colors = SLSettings::get()->layoutGroundColor;

    for (auto& theme : s_themes) {
        theme.initialize();
    }

    m_theme = &s_themes[SLSettings::get()->theme];
    m_state.m_themeState.selectedIndex = SLSettings::get()->theme;
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
    // The blur renderer needs desktop framebuffer blitting APIs that are not
    // available in Geometry Dash's iOS OpenGL ES context.
    useShader = false;
#endif
    rounding *= slui::Config::get().uiScale;
    borderSize *= slui::Config::get().uiScale;

    bgOpacity = std::pow(bgOpacity, Bot::get()->ui().m_theme->m_opacityExp);

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
    if (mods & SLKeybind<bool>::MODIFIER_CTRL)  name += "Ctrl+";
    if (mods & SLKeybind<bool>::MODIFIER_SHIFT) name += "Shift+";
    if (mods & SLKeybind<bool>::MODIFIER_ALT)   name += "Alt+";
    name += keyCodeName(key);
    return name;
}

static std::string keybindDisplayName_fromParts(int key, int mods) {
    std::string name;
    if (mods & SLKeybind<bool>::MODIFIER_CTRL)  name += "Ctrl+";
    if (mods & SLKeybind<bool>::MODIFIER_SHIFT) name += "Shift+";
    if (mods & SLKeybind<bool>::MODIFIER_ALT)   name += "Alt+";
    name += keyCodeName(key);
    return name;
}

void UIManager::drawKeybindContextMenu() {
    auto* bm  = SLBindingManager::get();
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
            const ImVec2 winSize{380.0f * scale, 0.0f};
            ImGui::SetNextWindowPos(
                ImGui::GetMainViewport()->GetCenter(),
                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(1.0f);
            if (ImGui::Begin("##SLKeyCapture", nullptr,
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
            return;
        }
    }

    if (!ctx.open) return;

    const float scale = slui::Config::get().uiScale;
    const ImVec2 winSize{380.0f * scale, 0.0f};
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(winSize, ImVec2(winSize.x, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(1.0f);

    if (!ImGui::Begin("##SLKeybindWindow", &ctx.open,
                      ImGuiWindowFlags_NoDecoration |
                      ImGuiWindowFlags_NoNav |
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    const float contentWidth = ImGui::GetContentRegionAvail().x;
    slui::group([&]() {
        verticalSpacer(8.0f);
        slui::text("Keybinds", m_bold);
        verticalSpacer(4.0f);
        slui::text(ctx.tag);
        slui::divider(false);

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
                    auto path = silicate::paths::file("keybinds.json");
                    bm->writeToFile(path);
                    break;
                }
                verticalSpacer(6.0f);
            }
            verticalSpacer(4.0f);
        }

        slui::divider(false);
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
        slui::divider(false);
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
                    auto path = silicate::paths::file("keybinds.json");
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
}

void UIManager::draw() {
#ifdef SILICATE_PROTECT
    VMProtectBegin("UIDraw");
#endif

    auto bot = Bot::get();
    auto& cfg = slui::Config::get();

    ImGuiHookCtx::get().m_time += cocos2d::CCDirector::get()->getDeltaTime();
    m_state.m_useShader->inner() = false;
    const auto popupShaderFn = []() {};

    cfg.uiScale = m_state.m_uiScale->inner() * getWindowDpi();
    cfg.fitWindowToContent = SLSettings::get()->fitMenuToContent;
    static constexpr float tabHeights[] = {390, 500, 280, 410, 500, 500, 530, 530};
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

    slui::ScopedFont s(m_font);
    
    static GLuint logoTex = 0;
    static int logoWidth = 1, logoHeight = 1;
    if (logoTex == 0) {
        GLint oldActiveTexture;
        GLint oldTexture;
        GLint oldUnpackAlignment;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldUnpackAlignment);

        int channels;
        auto logoPath = geode::Mod::get()->getResourcesDir() / "img" / "logo.png";
        if (!std::filesystem::exists(logoPath)) {
            logoPath = geode::Mod::get()->getResourcesDir() / "logo.png";
        }
        if (!std::filesystem::exists(logoPath)) {
            logoPath = geode::Mod::get()->getResourcesDir() / "title_new.png";
        }
        uint8_t* data = stbi_load(logoPath.string().c_str(), &logoWidth, &logoHeight, &channels, 4);
        if (data) {
            glGenTextures(1, &logoTex);
            glBindTexture(GL_TEXTURE_2D, logoTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, logoWidth, logoHeight, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        } else {
            logoTex = (GLuint)-1;
        }

        // Texture uploads use raw GL, whereas Cocos2d tracks texture state in
        // its own cache. Restore every state we touched before returning to
        // Cocos so the next sprite draw still sees its own atlas texture.
        glPixelStorei(GL_UNPACK_ALIGNMENT, oldUnpackAlignment);
        glBindTexture(GL_TEXTURE_2D, oldTexture);
        glActiveTexture(oldActiveTexture);
    }

    ImTextureID tex = (logoTex != 0 && logoTex != (GLuint)-1) ? (ImTextureID)(intptr_t)logoTex : (ImTextureID)(intptr_t)0;
    slui::window(tex, ImVec2((float)logoWidth, (float)logoHeight), [this, bot, popupShaderFn]() {
        {
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetWindowPos(),
                ImGui::GetWindowPos() + ImGui::GetWindowSize(),
                ImGui::GetColorU32(
                    ImVec4(0.1f, 0.1f, 0.1f, m_state.m_opacity->inner())),
                0.0f,
                ImDrawFlags_RoundCornersAll);
        }

        const auto tabButton = [this](const char* label, UIState::UITab tab) {
            const bool active = m_state.m_currentTab == tab;
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));
            }
            const bool pressed = ImGui::Button(label);
            if (active) ImGui::PopStyleColor(2);
            if (pressed) m_state.m_currentTab = tab;
            ImGui::SameLine();
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
        tabButton("Record", UIState::UITab::Record);
        tabButton("Assist", UIState::UITab::Assist);
        tabButton("Prediction", UIState::UITab::Prediction);
        tabButton("Edit", UIState::UITab::Edit);
        tabButton("Render", UIState::UITab::Render);
        tabButton("Settings", UIState::UITab::Settings);
        tabButton("Theme", UIState::UITab::Theme);
        ImGui::NewLine();
        ImGui::PopStyleVar();
        ImGui::Separator();

        ImGui::BeginChild("Content", ImVec2(0.0f, 0.0f), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        {
            if (!Bot::get()->isEnabled()) {
                slui::text("The bot is currently disabled.");
                if (GJBaseGameLayer::get()) {
                    slui::text(
                        "Please exit the level you're in to enable the bot.");
                } else {
                    if (slui::button("Enable").pressed) {
                        Bot::get()->m_enabled->inner() = true;
                        Bot::get()->m_enabled->notifyChange();
                    }
                }

                return;
            }

            slui::tab(m_state.m_currentTab, UIState::UITab::Record, [&]() {
                slui::text("Record", m_bold);

                slui::divider(false);
                auto& rs = Bot::get()->replaySystem();

                slui::text(fmt::format("Frame: {} - Macro Size: {}",
                                       bot->updater().getFrame(),
                                       rs.m_actionAtom.length()));

                if (ImGui::BeginTable("RecordMode", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    const bool recording = bot->isRecording();
                    if (recording) ImGui::PushStyleColor(
                        ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(recording ? "Stop Recording" : "Start Recording",
                                      ImVec2(-FLT_MIN, 0.0f))) {
                        bot->m_mode = recording ? Bot::Mode::Stopped
                                                : Bot::Mode::Recording;
                        if (PlayLayer::get() &&
                            bot->isRecording() &&
                            rs.getInputIndex() < rs.m_actionAtom.length()) {
                            rs.createBackup();
                            rs.m_actionAtom.clipActions(
                                bot->updater().getFrame());
                        }
                    }
                    if (recording) ImGui::PopStyleColor();

                    ImGui::TableNextColumn();
                    const bool playing = bot->isPlaying();
                    if (playing) ImGui::PushStyleColor(
                        ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(playing ? "Stop Playing" : "Start Playing",
                                      ImVec2(-FLT_MIN, 0.0f))) {
                        bot->m_mode = playing ? Bot::Mode::Stopped
                                              : Bot::Mode::Playing;
                    }
                    if (playing) ImGui::PopStyleColor();
                    ImGui::EndTable();
                }

                slui::divider();

                if (ImGui::BeginTable("RecordRate", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    if (slui::drag("TPS", bot->updater().m_tps->inner(), 0.0,
                                   std::numeric_limits<double>::max(), 1.0f,
                                   "{:g}")
                            .changed) {
                        bot->updater().m_tps->notifyChange();
                    }
                    keybindRightClick("updater.tps");

                    ImGui::TableNextColumn();
                    if (slui::drag("Speed", bot->updater().m_speedhack->inner(),
                                   0.0, std::numeric_limits<double>::max(),
                                   0.01f, "{:.2G}x")
                            .changed) {
                        bot->updater().m_speedhack->notifyChange();
                    }
                    keybindRightClick("updater.speedhack");
                    ImGui::EndTable();
                }

                if (ImGui::BeginTable("RecordPlaybackOptions", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Audio Speedhack",
                                   bot->updater().m_speedhackAudio->inner());
                    keybindRightClick("updater.speedhack_audio");
                    ImGui::TableNextColumn();
                    slui::checkbox("Block/Ignore Inputs During Playback",
                                   bot->replaySystem().m_ignoreInputs->inner());
                    keybindRightClick("replay.ignore_inputs");
                    ImGui::EndTable();
                }

                slui::divider();

                if (ImGui::BeginTable("RecordSaveLoad", 3, ImGuiTableFlags_None)) {
                    ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Load", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (slui::input_text_autocomplete(
                            "##ReplayName", "Replay", rs.m_replayName,
                            m_replayAutocomplete, popupShaderFn)
                            .changed) {
                        m_replayAutocomplete.suggestions = filterCandidates(
                            m_state.m_replayNames, rs.m_replayName);
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Save")) {
                        auto path = rs.getCurrentPath();
                        rs.backupExisting(path);
                        rs.save(path);

                        m_state.m_replayNames.clear();
                        for (const auto& entry :
                             std::filesystem::directory_iterator(
                                 silicate::paths::directory("replays"))) {
                            if (entry.is_regular_file() &&
                                (entry.path().extension() == ".grape" ||
                                 entry.path().extension() == ".slc")) {
                                m_state.m_replayNames.push_back(
                                    entry.path().stem().string());
                            }
                        }
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Load")) {
                        rs.load(rs.getCurrentPath());
                    }
                    ImGui::EndTable();
                }

                if (m_state.m_lastReplayName != rs.m_replayName) {
                    m_state.m_lastReplayName = rs.m_replayName;
                    m_state.m_replayNames.clear();
                    for (const auto& entry :
                         std::filesystem::directory_iterator(
                              silicate::paths::directory("replays"))) {
                        if (entry.is_regular_file() &&
                            (entry.path().extension() == ".grape" ||
                             entry.path().extension() == ".slc")) {
                            m_state.m_replayNames.push_back(
                                entry.path().stem().string());
                        }
                    }
                }

                slui::divider();

                if (ImGui::BeginTable("FrameAdvance", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Frame Advance",
                                    Bot::get()->updater().m_paused->inner());
                    keybindRightClick("updater.frame_advance");
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Advance", ImVec2(-FLT_MIN, 0.0f))) {
                        bot->updater().stepOnce();
                    }
                    keybindRightClick("updater.advance_one");
                    ImGui::EndTable();
                }

                if (ImGui::BeginTable("SeedOverride", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Seed Override", rs.m_overrideSeed);
                    ImGui::TableNextColumn();
                    slui::drag("Seed", rs.m_overriddenSeed, 0ull,
                               std::numeric_limits<uint64_t>::max());
                    ImGui::EndTable();
                }

                slui::divider();

                if (ImGui::BeginTable("DeathExtraRow", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Intentional Death",
                                    Bot::get()->updater().m_canDie->inner());
                    keybindRightClick("updater.intentional_death");
                    ImGui::TableNextColumn();
                    slui::checkbox(
                        "Frame Extrapolation",
                        Bot::get()->updater().m_extrapolateFrames->inner());
                    keybindRightClick("updater.frame_extrapolation");
                    ImGui::EndTable();
                }

            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Assist, [&]() {
                slui::text("Assist", m_bold);

                slui::divider(false);

                slui::text("Macro Merge", m_medium);
                if (slui::input_text_autocomplete(
                        "Merge Replay", "Replay to merge",
                        m_state.m_mergeReplayName,
                        m_state.m_mergeAutocomplete, popupShaderFn)
                        .changed) {
                    m_state.m_mergeAutocomplete.suggestions = filterCandidates(
                        m_state.m_replayNames, m_state.m_mergeReplayName);
                }

                slui::dropdown("Merge Mode##MergeDropdown",
                                m_state.m_mergeModeState,
                                m_state.m_mergeModeState.selectedIndex,
                                [&]() {
                                    renderBlurBg(12.0f, 1.5f,
                                                 m_state.m_useShader->inner());
                                });

                if (slui::button("Merge").pressed) {
                    auto mergePath =
                        silicate::paths::directory("replays") /
                        (m_state.m_mergeReplayName + ".grape");
                    if (!std::filesystem::exists(mergePath)) {
                        mergePath.replace_extension(".slc");
                    }
                    using MergeMode = ReplaySystem::MergeMode;
                    MergeMode mode;
                    switch (m_state.m_mergeModeState.selectedIndex) {
                        case 0:  mode = MergeMode::P1FromOther; break;
                        case 1:  mode = MergeMode::P2FromOther; break;
                        default: mode = MergeMode::SwapPlayers; break;
                    }
                    Bot::get()->replaySystem().merge(mergePath, mode);
                }

                slui::divider();

                slui::text("Hitboxes", m_medium);
                slui::checkbox("Toggle##Hitboxes",
                                Bot::get()->hitboxes().m_enabled->inner());
                keybindRightClick("hitboxes.enabled");

                slui::checkbox("Show Trail##Hitboxes",
                                Bot::get()->hitboxes().m_trailEnabled->inner());
                keybindRightClick("hitboxes.trail_enabled");

                if (Bot::get()->hitboxes().m_trailEnabled->inner()) {
                    slui::drag("Trail Length##Hitboxes",
                                SLSettings::get()->hitboxes.trailMaxLength,
                                50, 4000, 1.0f, "{:d}");

                    slui::drag("Trail Quality##Hitboxes",
                                SLSettings::get()->hitboxes.trailRebuildInterval,
                                1, 10, 1.0f, "Rebuild every {:d} steps");
                }

                slui::drag("Width##Hitboxes",
                            Bot::get()->hitboxes().m_width->inner(), 0.0, 1.0,
                            0.02f, "{:.2f}");

                slui::dropdown("Hitbox##Selector", m_state.m_hitboxState,
                                m_state.m_hitboxState.selectedIndex, [&]() {
                                    renderBlurBg(12.0f, 1.5f,
                                                 m_state.m_useShader->inner());
                                });

                {
                    auto& h = m_state.m_hitboxCategories[m_state.m_hitboxState
                                                             .selectedIndex];

                    auto& category = SLSettings::get()->hitboxes.categories[h];

                    slui::checkbox("Enabled##SpecificHitbox",
                                    category.enabled);

                    m_state.m_hitboxColorState.colors = category.colors;

                    slui::color("Color##SpecificHitbox",
                                 m_state.m_hitboxColorState, [&]() {
                                     renderBlurBg(12.0f, 1.5f,
                                                  m_state.m_useShader->inner());
                                 });

                    category.colors = m_state.m_hitboxColorState.colors;

                    slui::drag("Fill Opacity##SpecificHitbox",
                                category.fillOpacity, 0.0, 1.0, 0.01, "{:.2f}");
                };

                slui::divider();

                slui::text("Layout", m_medium);

                if (slui::checkbox("Enabled##LayoutMode",
                                    Bot::get()->updater().m_layoutMode->inner())
                        .pressed) {
                    Bot::get()->updater().m_layoutMode->notifyChange();
                }
                keybindRightClick("updater.layout_mode");

                slui::checkbox("Use Regular Background##LayoutMode",
                                Bot::get()->updater().m_useRegularBg->inner());
                keybindRightClick("updater.use_regular_bg");

                slui::color("Background Color##LayoutMode",
                             m_state.m_bgColorState, popupShaderFn);
                slui::color("Ground Color##LayoutMode",
                             m_state.m_groundColorState, popupShaderFn);

                SLSettings::get()->layoutBgColor =
                    m_state.m_bgColorState.colors;
                SLSettings::get()->layoutGroundColor =
                    m_state.m_groundColorState.colors;

                slui::divider();

                slui::text("Noclip", m_medium);

                if (ImGui::BeginTable("NoclipRow", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Enabled##Noclip",
                                   Bot::get()->updater().m_noclip->inner());
                    keybindRightClick("updater.noclip");
                    ImGui::TableNextColumn();
                    slui::dropdown("Player##Noclip", m_state.m_noclipState,
                                   *reinterpret_cast<int*>(
                                       &Bot::get()->updater().m_noclipType),
                                   popupShaderFn);
                    SLSettings::get()->noclipPlayer = static_cast<int>(
                        Bot::get()->updater().m_noclipType);
                    ImGui::EndTable();
                }

                slui::divider();

                slui::text("Trajectory", m_medium);

                slui::checkbox(
                    "Enabled##Trajectory",
                    Bot::get()->trajectory().m_state.m_enabled->inner());
                keybindRightClick("trajectory.enabled");
                slui::drag("Width##Trajectory",
                            Bot::get()->trajectory().m_state.m_width->inner(),
                            0.0, 1.0, 0.01f, "{:.2f}");
                slui::drag("Length##Trajectory",
                            Bot::get()->trajectory().m_state.m_length->inner(),
                            0.0, 5.0, 0.01f, "{:.2f}s");

                {
                    auto& maxSteps = SLSettings::get()->trajectory.maxSteps;
                    slui::drag("Max Steps##Trajectory", maxSteps,
                                0, 100000, 1.0f,
                                maxSteps == 0 ? "Unlimited" : "{:d} steps");
                    if (maxSteps < 0) maxSteps = 0;
                }

                slui::dropdown(
                    "Trajectory##Selector", m_state.m_trajectoryState,
                    m_state.m_trajectoryState.selectedIndex, [&]() {
                        renderBlurBg(12.0f, 1.5f, m_state.m_useShader->inner());
                    });

                {
                    auto& t = m_state.m_categories[m_state.m_trajectoryState
                                                       .selectedIndex];

                    auto& category =
                        SLSettings::get()->trajectory.categories[t];

                    slui::checkbox("Enabled##SpecificTrajectory",
                                    category.enabled);

                    m_state.m_trajectoryColorState.colors = category.colors;

                    slui::color("Color##SpecificTrajectory",
                                 m_state.m_trajectoryColorState, [&]() {
                                     renderBlurBg(12.0f, 1.5f,
                                                  m_state.m_useShader->inner());
                                 });

                    category.colors = m_state.m_trajectoryColorState.colors;
                };

                slui::divider();

                slui::text("Backstepping", m_medium);

                if (ImGui::BeginTable("BackwardsSteppingRow", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextColumn();
                        slui::checkbox(
                            "Backwards Stepping",
                            Bot::get()->updater().m_backwardsStepping->inner());
                        keybindRightClick("updater.backwards_stepping");

                        ImGui::TableNextColumn();
                        if (slui::drag("Steps",
                                        Bot::get()
                                            ->practiceFix()
                                            .m_maxStoredFrames->inner(),
                                        1u, 2400u)
                                .changed) {
                            Bot::get()
                                ->practiceFix()
                                .m_maxStoredFrames->notifyChange();
                        }
                    ImGui::EndTable();
                }

                slui::divider();

                slui::text("Autoclicker", m_medium);

                if (ImGui::BeginTable("AutoclickerRow", 2,
                                      ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    slui::checkbox("Enabled##Autoclicker",
                                   Bot::get()->autoclicker().m_enabled->inner());
                    keybindRightClick("autoclicker.enabled");
                    ImGui::TableNextColumn();
                    slui::dropdown(
                        "Player##Autoclicker", m_state.m_autoclickerState,
                        *reinterpret_cast<int*>(
                            &Bot::get()->autoclicker().m_player), popupShaderFn);
                    ImGui::EndTable();
                }
                slui::drag("Interval##Autoclicker",
                            Bot::get()->autoclicker().m_frequency, 0,
                            std::numeric_limits<int>::max(), 1.0f,
                            "{} Frame(s)");
                slui::checkbox("Swift Clicks",
                                Bot::get()->autoclicker().m_performSwifts);
                Bot::get()->autoclicker().saveSettings();
                keybindRightClick("autoclicker.swift_clicks");

                slui::divider();

                slui::text("Other", m_medium);

                slui::checkbox("Mirror Inputs",
                                Bot::get()->replaySystem().m_mirrorInputs);
                keybindRightClick("replay.mirror_inputs");
                slui::checkbox("Mirror Inverted",
                                Bot::get()->replaySystem().m_mirrorInverted);
                keybindRightClick("replay.mirror_inverted");

                slui::checkbox("Maintain Gravity",
                                Bot::get()->replaySystem().m_maintainGravity);
                keybindRightClick("replay.maintain_gravity");

                slui::checkbox("No Mirror Portals",
                                Bot::get()->updater().m_noMirror->inner());
                keybindRightClick("updater.no_mirror");
            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Prediction, [&]() {
                slui::text("Prediction", m_bold);

                slui::divider(false);

                slui::text("Automation", m_medium);

                slui::checkbox("Prevent Death",
                                Bot::get()->updater().m_preventDeath->inner());
                keybindRightClick("updater.prevent_death");
                slui::checkbox(
                    "Use Trajectory##PD",
                    Bot::get()->updater().m_fullGamePrediction->inner());
                keybindRightClick("updater.full_game_prediction");
                
                slui::divider();

                slui::text("Simulation", m_medium);

                if (slui::button("Find Best Frame").pressed) {
                    Bot::get()->updater().findBestFrameCandidate();
                }

                slui::drag(
                    "Threshold##Prediction",
                    Bot::get()->updater().m_acceptablePrediction->inner(), 0.0f,
                    1.0f, 0.01f, "{:.2f}");

            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Edit, [&]() {
                slui::text("Edit", m_bold);

                slui::divider(false);

                auto& replay = Bot::get()->replaySystem().m_actionAtom;
                auto& inputs = replay.m_actions;
                int inputIndex = Bot::get()->replaySystem().getInputIndex();

                if (inputs.empty()) {
                    slui::text("No replay loaded.");
                    m_state.m_editIndex = 0;
                } else {
                    m_state.m_editIndex = std::clamp(
                        m_state.m_editIndex, 0, static_cast<int>(inputs.size()) - 1);
                }

                float btnRows = 3.0f;
                float tableHeight = (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y) * btnRows + ImGui::GetStyle().ItemSpacing.y * 2.0f;
                float boxHeight = ImGui::GetContentRegionAvail().y - tableHeight;
                if (boxHeight < 100.0f * slui::Config::get().uiScale)
                    boxHeight = 100.0f * slui::Config::get().uiScale;
                const float boxWidth = (ImGui::GetContentRegionAvail().x -
                                        ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                ImGui::BeginChild("InputList", ImVec2(boxWidth, boxHeight), true);
                for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
                    const auto& input = inputs[i];
                    std::string label = fmt::format("{} at {}##Input{}",
                        input.m_holding ? "Click" : "Release", input.m_frame, i);
                    if (ImGui::Selectable(label.c_str(), m_state.m_editIndex == i))
                        m_state.m_editIndex = i;
                    if (i == inputIndex) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("InputEditor", ImVec2(0.0f, boxHeight), true);
                if (!inputs.empty()) {
                    int i = m_state.m_editIndex;
                    auto& input = inputs[i];
                    if (slui::drag("Frame##SelectedInput", input.m_frame, 0ull,
                                   std::numeric_limits<uint64_t>::max()).changed) {
                        input.recalculateDelta(i == 0 ? 0 : inputs[i - 1].m_frame);
                        if (i + 1 < static_cast<int>(inputs.size()))
                            inputs[i + 1].recalculateDelta(input.m_frame);
                        Bot::get()->updater().m_tps->notifyChange();
                    }
                    if (slui::checkbox("Holding / Click", input.m_holding).pressed)
                        Bot::get()->updater().m_tps->notifyChange();
                    if (slui::checkbox("Player 2", input.m_player2).pressed)
                        Bot::get()->updater().m_tps->notifyChange();
                }
                ImGui::EndChild();

                if (ImGui::BeginTable("MacroButtons", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Add Input", ImVec2(-FLT_MIN, 0.0f))) {
                        if (inputs.empty()) {
                            inputs.emplace_back(0, 0,
                                slc::v3::Action::ActionType::Jump, true, false);
                            m_state.m_editIndex = 0;
                        } else {
                            int i = m_state.m_editIndex;
                            auto next = slc::v3::Action(inputs[i].m_frame, 0,
                                slc::v3::Action::ActionType::Jump,
                                !inputs[i].m_holding, inputs[i].m_player2);
                            inputs.insert(inputs.begin() + i + 1, next);
                            m_state.m_editIndex = i + 1;
                            inputs[i + 1].recalculateDelta(inputs[i].m_frame);
                            if (i + 2 < static_cast<int>(inputs.size()))
                                inputs[i + 2].recalculateDelta(inputs[i + 1].m_frame);
                        }
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Delete Input", ImVec2(-FLT_MIN, 0.0f)) && !inputs.empty()) {
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
                    if (ImGui::Button("Remove P1 Inputs", ImVec2(-FLT_MIN, 0.0f))) {
                        inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [](auto& i) { return !i.m_player2; }), inputs.end());
                        for (size_t j = 0; j < inputs.size(); ++j) inputs[j].recalculateDelta(j == 0 ? 0 : inputs[j - 1].m_frame);
                        m_state.m_editIndex = 0;
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Remove P2 Inputs", ImVec2(-FLT_MIN, 0.0f))) {
                        inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [](auto& i) { return i.m_player2; }), inputs.end());
                        for (size_t j = 0; j < inputs.size(); ++j) inputs[j].recalculateDelta(j == 0 ? 0 : inputs[j - 1].m_frame);
                        m_state.m_editIndex = 0;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Flip Hold & Release", ImVec2(-FLT_MIN, 0.0f))) {
                        for (auto& i : inputs) i.m_holding = !i.m_holding;
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Flip P1 & P2", ImVec2(-FLT_MIN, 0.0f))) {
                        for (auto& i : inputs) i.m_player2 = !i.m_player2;
                    }

                    ImGui::EndTable();
                }
            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Render, [&]() {
                slui::text("Render", m_bold);

                slui::divider(false);

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

                                auto libDir = silicate::paths::directory("libraries");
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
                                SLSettings::get()->renderLabelsWhileRecording);
                keybindRightClick("render.labels_while_recording");

                slui::divider();

                slui::text("Presets", m_medium);

                if (ImGui::BeginTable("RenderPresets", 3, ImGuiTableFlags_None)) {
                    ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Load", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    slui::input_text_autocomplete(
                        "##PresetName", "Preset Name", m_state.m_presetName,
                        m_state.m_presetAutocomplete, [&]() {
                            renderBlurBg(12.0f, 1.5f, m_state.m_useShader->inner());
                        });
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Load##Preset")) {
                        auto path = silicate::paths::directory("presets") /
                                    (m_state.m_presetName + ".json");
                        if (std::filesystem::exists(path)) {
                            renderer->loadSettings(path);
                        } else {
                            geode::log::error("Preset file does not exist: {}",
                                              path.string());
                        }
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Save##Preset")) {
                        auto path = silicate::paths::directory("presets") /
                                    (m_state.m_presetName + ".json");
                        renderer->saveSettings(path);
                    }
                    ImGui::EndTable();
                }

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

                slui::input_text("Codec", "Codec",
                                  renderer->m_settings.m_codec);
                slui::input_text("Extension", "Extension",
                                  renderer->m_settings.m_extension);
                slui::drag("After End Time",
                            renderer->m_settings.m_afterEndTime, 0.0f, 10000.0f,
                            1.0f, "{:.2f}s");
                if (m_state.m_showExperimentalFeatures) {
                    slui::checkbox("SSB Fix",
                                    Bot::get()->updater().m_ssbFix->inner());
                }

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
                slui::text("Settings", m_bold);

                slui::divider(false);

                slui::text("Interface", m_medium);

                if (slui::button("Open Grape Folder").pressed) {
                    geode::utils::file::openFolder(
                        silicate::paths::dataRoot());
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

                slui::text("Backups", m_medium);

                slui::checkbox(
                    "Auto-Save At Level End",
                    Bot::get()->replaySystem().m_autosaveAtLevelEnd->inner());
                keybindRightClick("replay.autosave_at_level_end");

                auto& rs = Bot::get()->replaySystem();
                if (slui::checkbox("Auto-Backup",
                                    Bot::get()
                                        ->replaySystem()
                                        .m_autosaveAtInterval->inner())
                        .pressed) {
                    rs.m_autosaveAtInterval->notifyChange();
                }

                if (rs.m_autosaveAtInterval->inner()) {
                    if (slui::drag("Auto-Backup Interval",
                                    rs.m_autosaveInterval->inner(), 1.0, 3600.0,
                                    1.0, "{:.0f}s")
                            .changed) {
                        rs.m_autosaveInterval->notifyChange();
                    }
                }

                slui::divider();

                slui::text("Labels", m_medium);

                slui::checkbox("Display Labels",
                                Bot::get()->labels().m_globalEnabled);
                keybindRightClick("labels.global_enabled");

                slui::dropdown("Label##Selector", m_state.m_labelState,
                                m_state.m_labelState.selectedIndex, [&]() {
                                    renderBlurBg(12.0f, 1.5f,
                                                 m_state.m_useShader->inner());
                                });

                auto& label = Bot::get()
                                  ->labels()
                                  .m_labels[m_state.m_labelState.selectedIndex]
                                  .m_config;

                m_state.m_labelFontsState.selectedIndex =
                    (label.m_font == Label::LabelFont::BigFont) ? 0 : 1;

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
                    label.m_font = (idx == 0)
                        ? Label::LabelFont::BigFont
                        : Label::LabelFont::ChatFont;
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

                slui::checkbox("Lock Delta",
                                bot->updater().m_lockDelta->inner());
                keybindRightClick("updater.lock_delta");

                slui::dropdown("Lock Delta Mode", m_state.m_lockDeltaState,
                                bot->updater().m_lockDeltaMode->inner(), [&]() {
                                    renderBlurBg(12.0f, 1.5f,
                                                 m_state.m_useShader->inner());
                                });

                slui::checkbox("Real Time",
                                bot->updater().m_realTime->inner());
                keybindRightClick("updater.real_time");

                if (!bot->updater().m_realTime->inner()) {
                    slui::checkbox("Dynamic UPR",
                                    bot->updater().m_dynamicUpr->inner());

                    if (bot->updater().m_dynamicUpr->inner()) {
                        slui::drag("Target FPS",
                                    bot->updater().m_fpsTarget->inner(), 1.0,
                                    480.0, 1.0f, "{:.0f} FPS");
                    } else {
                        slui::drag("Max UPR", bot->updater().m_maxUPR->inner(),
                                    1u, 1000000u, 1.0f);

                        slui::checkbox(
                            "Use Visual Updates",
                            bot->updater().m_useVisualUpdates->inner());
                    }
                }

                slui::divider();

                slui::text("Miscellaneous", m_medium);

                slui::checkbox(
                    "Use Alternate Input Hook",
                    bot->replaySystem().m_useAlternateHook->inner());
                keybindRightClick("replay.use_alternate_hook");

                if (slui::button("Disable Bot").pressed) {
                    Bot::get()->m_enabled->inner() = false;
                    Bot::get()->m_enabled->notifyChange();
                }

                slui::checkbox("Experimental Features",
                                m_state.m_showExperimentalFeatures);
            });

            slui::tab(m_state.m_currentTab, UIState::UITab::Theme, [&]() {
                drawImGuiThemeEditor(m_bold);
            });
        }
        ImGui::EndChild();

        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetWindowPos(),
            ImGui::GetWindowPos() + ImGui::GetWindowSize(),
            ImGui::GetColorU32(ImVec4(1.0, 1.0, 1.0, 0.1f)),
            0.0f,
            ImDrawFlags_RoundCornersAll,
            1.0f * slui::Config::get().uiScale);

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

#ifdef SILICATE_PROTECT
    VMProtectEnd();
#endif
}
