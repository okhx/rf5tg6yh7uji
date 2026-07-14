#pragma once

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace slui {

struct WidgetState {
    bool pressed = false;
    bool hovered = false;
    bool held = false;
    bool changed = false;
};

struct DropdownState {
    std::vector<std::string> options;
    int selectedIndex = -1;
};

struct AutocompleteState {
    std::vector<std::string> suggestions;
};

struct ColorState {
    std::array<float, 4> colors{};
};

using Font = ImFont*;

struct Config {
    float uiScale = 1.0f;
    float widgetWidth = 180.0f;
    float animationSpeed = 1.0f;
    bool playAnimations = true;
    bool fitWindowToContent = false;
    float fittedWindowHeight = 460.0f;

    static Config& get() {
        static Config config;
        return config;
    }
};

class ScopedFont {
public:
    explicit ScopedFont(ImFont* font) : m_pushed(font != nullptr) {
        if (m_pushed) ImGui::PushFont(font);
    }
    ~ScopedFont() {
        if (m_pushed) ImGui::PopFont();
    }
private:
    bool m_pushed;
};

inline WidgetState state(bool changed) {
    return {changed, ImGui::IsItemHovered(), ImGui::IsItemActive(), changed};
}

inline WidgetState button(std::string_view label) {
    const bool value = ImGui::Button(std::string(label).c_str());
    return state(value);
}

inline WidgetState button_selector(std::string_view label, bool selected) {
    const bool value = ImGui::Selectable(std::string(label).c_str(), selected);
    return state(value);
}

inline WidgetState checkbox(std::string_view label, bool& value) {
    return state(ImGui::Checkbox(std::string(label).c_str(), &value));
}

template <class T, class U>
WidgetState radio(T& value, U option, std::string_view label) {
    const bool selected = value == static_cast<T>(option);
    const bool pressed = ImGui::RadioButton(std::string(label).c_str(), selected);
    if (pressed) value = static_cast<T>(option);
    return state(pressed);
}

template <class T>
WidgetState drag(std::string_view label, T& value, T min = T{}, T max = T{100},
                 float speed = 1.0f, std::string_view = {}) {
    ImGuiDataType type;
    if constexpr (std::is_same_v<T, float>) type = ImGuiDataType_Float;
    else if constexpr (std::is_same_v<T, double>) type = ImGuiDataType_Double;
    else if constexpr (std::is_same_v<T, int>) type = ImGuiDataType_S32;
    else if constexpr (std::is_same_v<T, unsigned int>) type = ImGuiDataType_U32;
    else if constexpr (std::is_same_v<T, uint64_t>) type = ImGuiDataType_U64;
    else if constexpr (std::is_same_v<T, int64_t>) type = ImGuiDataType_S64;
    else static_assert(sizeof(T) == 0, "Unsupported ImGui drag type");
    return state(ImGui::DragScalar(std::string(label).c_str(), type, &value,
                                   speed, &min, &max));
}

inline WidgetState input_text(std::string_view label, std::string_view hint,
                              std::string& value) {
    return state(ImGui::InputTextWithHint(std::string(label).c_str(),
                                         std::string(hint).c_str(), &value));
}

inline WidgetState input_text_autocomplete(
    std::string_view label, std::string_view hint, std::string& value,
    AutocompleteState& autocomplete, std::function<void()> = {}) {
    const bool changed = input_text(label, hint, value).changed;
    if (ImGui::IsItemActive() && !autocomplete.suggestions.empty())
        ImGui::OpenPopup((std::string(label) + "##suggestions").c_str());
    const std::string popup = std::string(label) + "##suggestions";
    if (ImGui::BeginPopup(popup.c_str())) {
        for (const auto& suggestion : autocomplete.suggestions) {
            if (ImGui::Selectable(suggestion.c_str())) {
                value = suggestion;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    return state(changed);
}

inline WidgetState dropdown(std::string_view label, DropdownState& stateData,
                            int& selected, std::function<void()> = {},
                            bool = true) {
    const char* preview = selected >= 0 &&
            selected < static_cast<int>(stateData.options.size())
        ? stateData.options[selected].c_str() : "Select";
    bool changed = false;
    if (ImGui::BeginCombo(std::string(label).c_str(), preview)) {
        for (int i = 0; i < static_cast<int>(stateData.options.size()); ++i) {
            if (ImGui::Selectable(stateData.options[i].c_str(), selected == i)) {
                selected = i;
                stateData.selectedIndex = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return state(changed);
}

inline WidgetState color(std::string_view label, ColorState& value,
                         std::function<void()> = {}) {
    return state(ImGui::ColorEdit4(std::string(label).c_str(), value.colors.data(),
                                   ImGuiColorEditFlags_AlphaBar));
}

inline void text(std::string_view value, ImFont* font = nullptr) {
    ScopedFont scoped(font);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}
inline void same_line() { ImGui::SameLine(); }
inline void spacer(double size = 0.0) { ImGui::Dummy(ImVec2(0.0f, size)); }
inline void divider(bool = true) { ImGui::Separator(); }
inline void off_the_screen() { ImGui::SetCursorPos(ImVec2(-10000.0f, -10000.0f)); }

template <class F> void group(F&& fn, float width = 0.0f) {
    ImGui::BeginGroup();
    if (width > 0.0f) ImGui::PushItemWidth(width);
    fn();
    if (width > 0.0f) ImGui::PopItemWidth();
    ImGui::EndGroup();
}
template <class F> void fraction(double count, F&& fn, double = 0.0) {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / count);
    fn();
    ImGui::PopItemWidth();
}
template <class T, class F> void tab(T current, T expected, F&& fn) {
    if (current == expected) fn();
}
template <class F> void window(ImTextureID logoTex, ImVec2 logoSize, F&& fn) {
    const float scale = Config::get().uiScale;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (Config::get().fitWindowToContent) {
        ImGui::SetNextWindowSize(
            ImVec2(std::min(620.0f * scale, viewport->WorkSize.x * 0.92f),
                   std::min(Config::get().fittedWindowHeight * scale,
                            viewport->WorkSize.y * 0.90f)),
            ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 460.0f * scale),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f, 320.0f),
                                            viewport->WorkSize);
    }
    if (ImGui::Begin("Main UI", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse)) {
        const float titleHeight = 28.0f * scale;
        const ImVec2 titlePos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##titlebar", ImVec2(ImGui::GetContentRegionAvail().x,
                                                    titleHeight));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 titleMax(titlePos.x + ImGui::GetContentRegionAvail().x,
                             titlePos.y + titleHeight);
        draw->AddRectFilled(titlePos, titleMax,
                            ImGui::GetColorU32(ImGuiCol_TitleBgActive),
                            ImGui::GetStyle().WindowRounding,
                            ImDrawFlags_RoundCornersTop);
        if (logoTex) {
            float drawHeight = titleHeight * 0.75f;
            float drawWidth = logoSize.x * (drawHeight / logoSize.y);
            ImVec2 pMin = ImVec2(titlePos.x + (titleMax.x - titlePos.x - drawWidth) * 0.5f,
                                 titlePos.y + (titleHeight - drawHeight) * 0.5f);
            ImVec2 pMax = ImVec2(pMin.x + drawWidth, pMin.y + drawHeight);
            
            // Drop shadow to make the logo stand out
            ImVec2 shadowOffset(2.0f, 2.0f);
            draw->AddImage(logoTex, ImVec2(pMin.x + shadowOffset.x, pMin.y + shadowOffset.y), 
                           ImVec2(pMax.x + shadowOffset.x, pMax.y + shadowOffset.y), 
                           ImVec2(0,0), ImVec2(1,1), IM_COL32(0, 0, 0, 180));
                           
            draw->AddImage(logoTex, pMin, pMax, ImVec2(0,0), ImVec2(1,1), IM_COL32(255, 255, 255, 255));
        }
        constexpr std::string_view credit = "Silicate \xe2\x99\xa5";
        const ImVec2 creditSize = ImGui::CalcTextSize(
            credit.data(), credit.data() + credit.size());
        ImVec4 creditColor = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
        draw->AddText(ImVec2(titleMax.x - creditSize.x - 10.0f,
                             titlePos.y + (titleHeight - creditSize.y) * 0.5f),
                      ImGui::GetColorU32(creditColor), credit.data(),
                      credit.data() + credit.size());
        fn();
    }
    ImGui::End();
}
template <class F> void section(std::string_view id, F&& fn,
                                float width = 0.0f, bool = false) {
    if (width > 0.0f) {
        ImGui::BeginChild(std::string(id).c_str(), ImVec2(width, 0.0f), true);
        fn();
        ImGui::EndChild();
        ImGui::SameLine();
    } else {
        ImGui::BeginChild(std::string(id).c_str(), ImVec2(0.0f, 0.0f), true);
        fn();
        ImGui::EndChild();
    }
}

}  // namespace slui
