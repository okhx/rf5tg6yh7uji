#pragma once

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <charconv>
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

#ifdef GRAPE_PRIVATE_PC
#include "skrt_ui.hpp"
#endif

namespace slui {

void bringCurrentWindowToFront();

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
    std::string hex;
};

using Font = ImFont*;

struct Config {
    float uiScale = 1.0f;
    float widgetWidth = 180.0f;
    float animationSpeed = 1.0f;
    bool playAnimations = true;
    bool skeetMode = false;
    ImFont* skeetHeaderFont = nullptr;
    bool skeetGridStarted = false;
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

inline std::string_view visible_label(std::string_view label) {
    const auto marker = label.find("##");
    return label.substr(0, marker);
}

inline bool raw_button(std::string_view label, ImVec2 requested = ImVec2()) {
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode) {
        return grape::pc::skrt::button(label, requested);
    }
#endif
    return ImGui::Button(std::string(label).c_str(), requested);
}

inline WidgetState button(std::string_view label) {
    return state(raw_button(label));
}

inline WidgetState button_selector(std::string_view label, bool selected) {
    if (Config::get().skeetMode) {
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(
                ImGuiCol_CheckMark));
        const bool value =
            ImGui::Selectable(std::string(label).c_str(), selected);
        if (selected) ImGui::PopStyleColor();
        return state(value);
    }
    const bool value = ImGui::Selectable(std::string(label).c_str(), selected);
    return state(value);
}

inline WidgetState checkbox(std::string_view label, bool& value) {
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode) {
        return state(grape::pc::skrt::checkbox(label, value));
    }
#endif
    return state(ImGui::Checkbox(std::string(label).c_str(), &value));
}

template <class T, class U>
WidgetState radio(T& value, U option, std::string_view label) {
    const bool selected = value == static_cast<T>(option);
    if (Config::get().skeetMode) {
        bool checked = selected;
        const bool pressed = checkbox(label, checked).pressed;
        if (pressed) value = static_cast<T>(option);
        return state(pressed);
    }
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
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode)
        return state(grape::pc::skrt::dragScalar(
            label, type, &value, &min, &max, speed));
#endif
    return state(ImGui::DragScalar(std::string(label).c_str(), type, &value,
                                   speed, &min, &max));
}

// A single 100%-wide track split into three draggable segments (a + b + c ==
// 100). Two dividers can be dragged to redistribute between adjacent segments.
inline WidgetState triple_slider(std::string_view label, float& a, float& b,
                                 float& c) {
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode)
        return state(grape::pc::skrt::tripleSlider(label, a, b, c));
#endif
    float sum = a + b + c;
    if (!(sum > 0.0f)) {
        a = b = c = 100.0f / 3.0f;
        sum = 100.0f;
    } else if (std::fabs(sum - 100.0f) > 0.001f) {
        a = a / sum * 100.0f;
        b = b / sum * 100.0f;
        c = c / sum * 100.0f;
    }

    const std::string_view shown = visible_label(label);
    if (!shown.empty())
        ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());

    const float width = std::max(1.0f, ImGui::CalcItemWidth());
    const float height = ImGui::GetFrameHeight();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    const std::string id = "##tri-" + std::string(label);
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));

    float d1 = a * 0.01f;
    float d2 = (a + b) * 0.01f;

    static ImGuiID s_activeId = 0;
    static int s_activeDivider = 0;

    bool changed = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float ratio = std::clamp(
            (ImGui::GetIO().MousePos.x - p0.x) / width, 0.0f, 1.0f);
        if (ImGui::IsItemActivated()) {
            s_activeId = ImGui::GetItemID();
            s_activeDivider =
                std::fabs(ratio - d1) <= std::fabs(ratio - d2) ? 1 : 2;
        }
        if (s_activeId == ImGui::GetItemID()) {
            if (s_activeDivider == 1) d1 = std::clamp(ratio, 0.0f, d2);
            else d2 = std::clamp(ratio, d1, 1.0f);
            a = d1 * 100.0f;
            b = (d2 - d1) * 100.0f;
            c = (1.0f - d2) * 100.0f;
            changed = true;
        }
    }

    auto* draw = ImGui::GetWindowDrawList();
    const float mid1 = p0.x + width * d1;
    const float mid2 = p0.x + width * d2;
    const ImU32 colA = IM_COL32(90, 200, 120, 255);
    const ImU32 colB = IM_COL32(90, 150, 235, 255);
    const ImU32 colC = IM_COL32(235, 165, 80, 255);
    draw->AddRectFilled(p0, ImVec2(mid1, p1.y), colA);
    draw->AddRectFilled(ImVec2(mid1, p0.y), ImVec2(mid2, p1.y), colB);
    draw->AddRectFilled(ImVec2(mid2, p0.y), p1, colC);
    draw->AddLine(ImVec2(mid1, p0.y), ImVec2(mid1, p1.y),
                  IM_COL32(15, 15, 15, 255), 2.0f);
    draw->AddLine(ImVec2(mid2, p0.y), ImVec2(mid2, p1.y),
                  IM_COL32(15, 15, 15, 255), 2.0f);
    draw->AddRect(p0, p1, IM_COL32(10, 10, 10, 255));

    const auto segLabel = [&](float loX, float hiX, float pct) {
        char buf[16]{};
        std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        if (hiX - loX < ts.x + 2.0f) return;
        const ImVec2 tp((loX + hiX - ts.x) * 0.5f,
                        p0.y + (height - ts.y) * 0.5f);
        draw->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 200), buf);
        draw->AddText(tp, IM_COL32(245, 245, 245, 255), buf);
    };
    segLabel(p0.x, mid1, a);
    segLabel(mid1, mid2, b);
    segLabel(mid2, p1.x, c);

    return {changed, ImGui::IsItemHovered(), ImGui::IsItemActive(), changed};
}

inline WidgetState input_text(std::string_view label, std::string_view hint,
                              std::string& value) {
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode)
        return state(grape::pc::skrt::inputText(label, hint, value));
#endif
    return state(ImGui::InputTextWithHint(std::string(label).c_str(),
                                         std::string(hint).c_str(), &value));
}

inline void next_input_full_width() {
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode)
        grape::pc::skrt::nextInputFullWidth();
#endif
}

inline WidgetState input_text_autocomplete(
    std::string_view label, std::string_view hint, std::string& value,
    AutocompleteState& autocomplete, std::function<void()> = {}) {
    bool changed = input_text(label, hint, value).changed;
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if ((ImGui::IsItemActivated() || ImGui::IsItemClicked() || changed) &&
        !autocomplete.suggestions.empty())
        ImGui::OpenPopup((std::string(label) + "##suggestions").c_str());
    const std::string popup = std::string(label) + "##suggestions";
    if (ImGui::BeginPopup(popup.c_str(),
                          ImGuiWindowFlags_NoFocusOnAppearing)) {
        bringCurrentWindowToFront();
        for (const auto& suggestion : autocomplete.suggestions) {
            if (ImGui::Selectable(suggestion.c_str())) {
                value = suggestion;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    return {changed, hovered, held, changed};
}

inline WidgetState dropdown(std::string_view label, DropdownState& stateData,
                            int& selected, std::function<void()> = {},
                            bool = true) {
    const char* preview = selected >= 0 &&
            selected < static_cast<int>(stateData.options.size())
        ? stateData.options[selected].c_str() : "Select";
    bool changed = false;
    std::string id(label);
    bool opened = false;
    bool hovered = false;
    bool held = false;
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode) {
        opened = grape::pc::skrt::beginCombo(label, preview);
        hovered = ImGui::IsItemHovered();
        held = ImGui::IsItemActive();
    } else
#endif
    {
        opened = ImGui::BeginCombo(id.c_str(), preview);
    }
    if (opened) {
        for (int i = 0; i < static_cast<int>(stateData.options.size()); ++i) {
            if (ImGui::Selectable(stateData.options[i].c_str(), selected == i)) {
                selected = i;
                stateData.selectedIndex = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (Config::get().skeetMode)
        return {changed, hovered, held, changed};
    return state(changed);
}

inline WidgetState color(std::string_view label, ColorState& value,
                         std::function<void()> = {}) {
    const std::string id(label);
    const auto syncHex = [&] {
        char hex[10];
        std::snprintf(
            hex, sizeof(hex), "#%02X%02X%02X%02X",
            static_cast<int>(std::clamp(value.colors[0], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(value.colors[1], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(value.colors[2], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(value.colors[3], 0.f, 1.f) * 255.f));
        value.hex = hex;
    };
    const auto applyHex = [&] {
        std::string_view hex = value.hex;
        if (hex.starts_with('#')) hex.remove_prefix(1);
        uint32_t color = 0;
        const auto parsed = std::from_chars(
            hex.data(), hex.data() + hex.size(), color, 16);
        if ((hex.size() != 6 && hex.size() != 8) ||
            parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size())
            return false;
        if (hex.size() == 6) color = color << 8 | 0xff;
        value.colors = {
            ((color >> 24) & 0xff) / 255.f,
            ((color >> 16) & 0xff) / 255.f,
            ((color >> 8) & 0xff) / 255.f,
            (color & 0xff) / 255.f,
        };
        return true;
    };

    const ImVec4 preview{value.colors[0], value.colors[1], value.colors[2],
                         value.colors[3]};
    bool pressed = false;
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode) {
        pressed = grape::pc::skrt::colorButton(label, preview);
    } else
#endif
    {
        pressed = ImGui::ColorButton(
            ("##color_button" + id).c_str(), preview,
            ImGuiColorEditFlags_AlphaPreviewHalf);
        ImGui::SameLine();
        const auto suffix = id.find("##");
        ImGui::TextUnformatted(id.data(), id.data() +
            (suffix == std::string::npos ? id.size() : suffix));
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (pressed) {
        syncHex();
        ImGui::OpenPopup(("##color_popup" + id).c_str());
    }
    bool changed = false;
    const bool styledPopup = Config::get().skeetMode;
    if (styledPopup) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.067f, 0.067f, 0.067f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    }
    if (ImGui::BeginPopup(("##color_popup" + id).c_str())) {
        if (ImGui::ColorPicker4(
                ("##color_picker" + id).c_str(), value.colors.data(),
                ImGuiColorEditFlags_AlphaBar |
                    ImGuiColorEditFlags_AlphaPreviewHalf |
                    ImGuiColorEditFlags_NoInputs)) {
            changed = true;
            syncHex();
        }

        if (ImGui::BeginTable(("##color_hex_row" + id).c_str(), 3)) {
            ImGui::TableSetupColumn(
                "Hex", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(
                "Copy", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(
                "Paste", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint(
                    ("##color_hex" + id).c_str(), "#RRGGBBAA", &value.hex))
                changed |= applyHex();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(("Copy##color_" + id).c_str())) {
                syncHex();
                ImGui::SetClipboardText(value.hex.c_str());
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(("Paste##color_" + id).c_str())) {
                value.hex =
                    ImGui::GetClipboardText() ? ImGui::GetClipboardText() : "";
                changed |= applyHex();
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
    if (styledPopup) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }
    return {pressed || changed, hovered, held, changed};
}

inline void text(std::string_view value, ImFont* font = nullptr) {
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode && font) {
        if (grape::pc::skrt::setSectionTitle(value)) return;
    }
#endif
    ScopedFont scoped(
        Config::get().skeetMode && font
            ? Config::get().skeetHeaderFont
            : Config::get().skeetMode ? nullptr : font);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}
inline void same_line() { ImGui::SameLine(); }
inline void spacer(double size = 0.0) { ImGui::Dummy(ImVec2(0.0f, size)); }
inline void divider(bool visible = true) {
    (void)visible;
    auto& config = Config::get();
    if (!config.skeetMode) {
        ImGui::Separator();
        return;
    }
#ifdef GRAPE_PRIVATE_PC
    if (!config.skeetGridStarted && !visible) {
        config.skeetGridStarted = grape::pc::skrt::beginSections();
    } else if (config.skeetGridStarted && visible) {
        grape::pc::skrt::nextSection();
    }
#endif
}
inline void hide_section_box() {
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode) grape::pc::skrt::hideSectionBox();
#endif
}
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
    if (current != expected) return;
    auto& config = Config::get();
    if (config.skeetMode) config.skeetGridStarted = false;
    fn();
    if (config.skeetMode && config.skeetGridStarted) {
#ifdef GRAPE_PRIVATE_PC
        grape::pc::skrt::endSections();
#endif
        config.skeetGridStarted = false;
    }
}
template <class F>
void window(ImTextureID logoTex, ImVec2 logoSize, ImVec2 logoUv, F&& fn) {
    const float scale = Config::get().uiScale;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
#ifdef GRAPE_PRIVATE_PC
    if (Config::get().skeetMode) {
        if (grape::pc::skrt::beginWindow()) fn();
        grape::pc::skrt::endWindow();
        return;
    }
#endif
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
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_None)) {
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
            
            ImVec2 shadowOffset(2.0f, 2.0f);
            draw->AddImage(logoTex, ImVec2(pMin.x + shadowOffset.x, pMin.y + shadowOffset.y), 
                           ImVec2(pMax.x + shadowOffset.x, pMax.y + shadowOffset.y), 
                           ImVec2(0,0), logoUv, IM_COL32(0, 0, 0, 180));
                           
            draw->AddImage(logoTex, pMin, pMax, ImVec2(0,0), logoUv, IM_COL32(255, 255, 255, 255));
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

}
