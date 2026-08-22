#pragma once

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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

#ifdef GRAPE_PC
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
    bool customMode = false;
    ImFont* customFont = nullptr;
    ImFont* mediumFont = nullptr;
    ImFont* customMediumFont = nullptr;
    ImFont* boldFont = nullptr;
    ImFont* customBoldFont = nullptr;
    const char* username = "";
    bool skeetMode = false;
    ImFont* skeetHeaderFont = nullptr;
    bool skeetGridStarted = false;
    bool customSectionStarted = false;
    bool customSectionWantsTitle = false;
    int customSectionIndex = 0;
    float customSectionWidth = 0.0f;
    int customSectionColumn = 0;
    ImVec2 customSectionOrigin{};
    std::array<float, 2> customSectionColumnY{};
    ImVec2 customSectionMin{};
    ImVec2 customSectionClipMin{};
    ImVec2 customSectionClipMax{};
    float customSectionHeight = 0.0f;
    std::string customSectionTitle;
    bool fitWindowToContent = false;
    float fittedWindowHeight = 460.0f;

    static Config& get() {
        static Config config;
        return config;
    }
};

class ScopedFont {
public:
    explicit ScopedFont(ImFont* font) {
        const auto& config = Config::get();
        if (config.customMode && config.customFont) {
            font = font == config.boldFont ? config.customBoldFont
                 : font == config.mediumFont ? config.customMediumFont
                 : config.customFont;
        }
        m_pushed = font != nullptr;
        if (m_pushed) ImGui::PushFont(font);
    }
    ~ScopedFont() {
        if (m_pushed) ImGui::PopFont();
    }
private:
    bool m_pushed = false;
};

inline WidgetState state(bool changed) {
    return {changed, ImGui::IsItemHovered(), ImGui::IsItemActive(), changed};
}

inline std::string_view visible_label(std::string_view label) {
    const auto marker = label.find("##");
    return label.substr(0, marker);
}

inline void align_custom_value(std::string_view label) {
    const auto shown = visible_label(label);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float available = ImGui::GetContentRegionAvail().x;
    const ImVec2 textSize = ImGui::CalcTextSize(
        shown.data(), shown.data() + shown.size());
    const float width = std::min(
        ImGui::CalcItemWidth(),
        std::max(1.0f, available - (shown.empty() ? 0.0f
            : textSize.x + ImGui::GetStyle().ItemInnerSpacing.x)));
    if (!shown.empty())
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(start.x, start.y +
                (ImGui::GetFrameHeight() - textSize.y) * .5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            shown.data(), shown.data() + shown.size());
    ImGui::SetCursorScreenPos(ImVec2(start.x + available - width, start.y));
    ImGui::SetNextItemWidth(width);
}

inline float animate_last_item(float target) {
    auto& config = Config::get();
    float* value = ImGui::GetStateStorage()->GetFloatRef(
        ImGui::GetItemID(), target);
    const float blend = config.playAnimations
        ? 1.0f - std::exp(-12.0f * config.animationSpeed *
                          ImGui::GetIO().DeltaTime)
        : 1.0f;
    *value += (target - *value) * std::clamp(blend, 0.0f, 1.0f);
    return *value;
}

inline bool custom_button(std::string_view label, ImVec2 requested) {
    const auto shown = visible_label(label);
    const ImVec2 textSize = ImGui::CalcTextSize(
        shown.data(), shown.data() + shown.size());
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    ImVec2 size = requested;
    if (size.x == 0.0f) size.x = textSize.x + padding.x * 2.0f;
    else if (size.x < 0.0f)
        size.x = std::max(1.0f, ImGui::GetContentRegionAvail().x + size.x);
    if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(
        std::string(label).c_str(), size);
    const float hover = animate_last_item(ImGui::IsItemHovered() ? 1.0f : 0.0f);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    draw->AddRectFilled(min, max, IM_COL32(24, 21, 24, 255), 4.0f);
    if (hover > .001f)
        draw->AddRectFilled(min, max,
            IM_COL32(255, 255, 255, static_cast<int>(20.0f * hover)), 4.0f);
    draw->AddRect(min, max, IM_COL32(34, 34, 34, 255), 4.0f);
    draw->AddText(ImVec2(min.x + (size.x - textSize.x) * .5f,
                         min.y + (size.y - textSize.y) * .5f),
                  IM_COL32_WHITE, shown.data(), shown.data() + shown.size());
    return pressed;
}

inline bool raw_button(std::string_view label, ImVec2 requested = ImVec2()) {
#ifdef GRAPE_PC
    if (Config::get().skeetMode) {
        return grape::pc::skrt::button(label, requested);
    }
    if (Config::get().customMode) return custom_button(label, requested);
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
#ifdef GRAPE_PC
    if (Config::get().skeetMode) {
        return state(grape::pc::skrt::checkbox(label, value));
    }
    if (Config::get().customMode) {
        const auto shown = visible_label(label);
        const float side = ImGui::GetFrameHeight();
        const ImVec2 textSize = ImGui::CalcTextSize(
            shown.data(), shown.data() + shown.size());
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 size(side + (shown.empty() ? 0.0f
            : ImGui::GetStyle().ItemInnerSpacing.x + textSize.x), side);
        const bool pressed = ImGui::InvisibleButton(
            std::string(label).c_str(), size);
        if (pressed) value = !value;
        const float checked = animate_last_item(value ? 1.0f : 0.0f);
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 boxMax(min.x + side, min.y + side);
        draw->AddRectFilled(min, boxMax,
                            IM_COL32(24, 21, 24, 255), 4.0f);
        draw->AddRect(min, boxMax, IM_COL32(34, 34, 34, 255), 4.0f);
        if (checked > .001f) {
            const ImU32 tick = IM_COL32(
                255, 255, 255, static_cast<int>(255.0f * checked));
            draw->AddLine(ImVec2(min.x + side * .24f, min.y + side * .52f),
                          ImVec2(min.x + side * .43f, min.y + side * .71f),
                          tick, 2.0f);
            draw->AddLine(ImVec2(min.x + side * .43f, min.y + side * .71f),
                          ImVec2(min.x + side * .78f, min.y + side * .30f),
                          tick, 2.0f);
        }
        if (!shown.empty())
            draw->AddText(
                ImVec2(min.x + side + ImGui::GetStyle().ItemInnerSpacing.x,
                       min.y + (side - textSize.y) * .5f),
                IM_COL32_WHITE, shown.data(), shown.data() + shown.size());
        return {pressed, ImGui::IsItemHovered(), ImGui::IsItemActive(), pressed};
    }
#endif
    return state(ImGui::Checkbox(std::string(label).c_str(), &value));
}

template <class T, class U>
WidgetState radio(T& value, U option, std::string_view label) {
    const bool selected = value == static_cast<T>(option);
    if (Config::get().skeetMode || Config::get().customMode) {
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
#ifdef GRAPE_PC
    if (Config::get().customMode) {
        const char* format;
        if constexpr (std::is_floating_point_v<T>) format = "%.3f";
        else if constexpr (std::is_signed_v<T>)
            format = sizeof(T) > sizeof(int) ? "%lld" : "%d";
        else format = sizeof(T) > sizeof(unsigned int) ? "%llu" : "%u";
        align_custom_value(label);
        const std::string id = "##value-" + std::string(label);
        const bool changed = ImGui::InputScalar(
            id.c_str(), type, &value, nullptr, nullptr, format,
            ImGuiInputTextFlags_AutoSelectAll);
        if (changed) value = std::clamp(value, min, max);
        return state(changed);
    }
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
#ifdef GRAPE_PC
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
    draw->AddRect(p0, p1, Config::get().customMode
        ? IM_COL32(34, 34, 34, 255) : IM_COL32(10, 10, 10, 255));

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
#ifdef GRAPE_PC
    if (Config::get().skeetMode)
        return state(grape::pc::skrt::inputText(label, hint, value));
#endif
    return state(ImGui::InputTextWithHint(std::string(label).c_str(),
                                         std::string(hint).c_str(), &value));
}

inline void next_input_full_width() {
#ifdef GRAPE_PC
    if (Config::get().skeetMode)
        grape::pc::skrt::nextInputFullWidth();
#endif
}

inline bool custom_dropdown_option(std::string_view label, bool selected);

inline WidgetState input_text_autocomplete(
    std::string_view label, std::string_view hint, std::string& value,
    AutocompleteState& autocomplete, std::function<void()> = {}) {
    bool changed = input_text(label, hint, value).changed;
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    float popupWidth = ImGui::GetItemRectSize().x;
    for (const auto& suggestion : autocomplete.suggestions)
        popupWidth = std::max(
            popupWidth, ImGui::CalcTextSize(suggestion.c_str()).x + 16.0f);
    if ((ImGui::IsItemActivated() || ImGui::IsItemClicked() || changed) &&
        !autocomplete.suggestions.empty())
        ImGui::OpenPopup((std::string(label) + "##suggestions").c_str());
    const std::string popup = std::string(label) + "##suggestions";
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(popupWidth, 0.0f), ImVec2(popupWidth, FLT_MAX));
    if (!autocomplete.suggestions.empty() &&
        ImGui::BeginPopup(popup.c_str(), ImGuiWindowFlags_NoFocusOnAppearing)) {
        bringCurrentWindowToFront();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsWindowHovered() && !hovered)
            ImGui::CloseCurrentPopup();
        for (int i = 0;
             i < static_cast<int>(autocomplete.suggestions.size()); ++i) {
            const auto& suggestion = autocomplete.suggestions[i];
            ImGui::PushID(i);
            const bool pressed = Config::get().customMode
                ? custom_dropdown_option(suggestion, false)
                : ImGui::Selectable(suggestion.c_str());
            ImGui::PopID();
            if (pressed) {
                value = suggestion;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    return {changed, hovered, held, changed};
}

inline bool custom_dropdown_option(std::string_view label, bool selected) {
    constexpr float padding = 4.0f;
    const ImVec2 textSize = ImGui::CalcTextSize(
        label.data(), label.data() + label.size());
    const auto windowPos = ImGui::GetWindowPos();
    const auto windowSize = ImGui::GetWindowSize();
    const float inset = ImGui::GetStyle().WindowPadding.x;
    const ImVec2 min(windowPos.x + inset, ImGui::GetCursorScreenPos().y);
    const float right = windowPos.x + windowSize.x - inset;
    ImGui::SetCursorScreenPos(min);
    const std::string id = "##dropdown-option-" + std::string(label);
    const bool pressed = ImGui::InvisibleButton(
        id.c_str(), ImVec2(std::max(1.0f, right - min.x),
                           textSize.y + padding * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    float* animation = ImGui::GetStateStorage()->GetFloatRef(
        ImGui::GetItemID(), 0.0f);
    const float target = hovered ? 1.0f : 0.0f;
    const auto& config = Config::get();
    const float blend = config.playAnimations
        ? 1.0f - std::exp(-12.0f * config.animationSpeed *
                          ImGui::GetIO().DeltaTime)
        : 1.0f;
    *animation += (target - *animation) * std::clamp(blend, 0.0f, 1.0f);

    auto* draw = ImGui::GetWindowDrawList();
    if (*animation > .001f) {
        draw->AddRectFilled(
            min, ImGui::GetItemRectMax(),
            IM_COL32(255, 255, 255,
                     static_cast<int>(255.0f * .20f * *animation)),
            4.0f);
    }
    draw->AddText(
        ImVec2(min.x + padding, min.y + padding),
        selected ? IM_COL32_WHITE : IM_COL32(204, 204, 204, 255),
        label.data(), label.data() + label.size());
    if (pressed) ImGui::CloseCurrentPopup();
    return pressed;
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
#ifdef GRAPE_PC
    if (Config::get().skeetMode) {
        opened = grape::pc::skrt::beginCombo(label, preview);
        hovered = ImGui::IsItemHovered();
        held = ImGui::IsItemActive();
    } else
#endif
    {
        if (Config::get().customMode) {
            auto* draw = ImGui::GetWindowDrawList();
            const auto padding = ImGui::GetStyle().FramePadding;
            align_custom_value(label);
            id = "##combo-" + std::string(label);
            const float popupWidth = ImGui::CalcItemWidth();
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(popupWidth, 0.0f), ImVec2(popupWidth, FLT_MAX));
            ImGui::PushStyleVar(
                ImGuiStyleVar_FramePadding,
                ImVec2(padding.x + 14.0f, padding.y));
            opened = ImGui::BeginCombo(
                id.c_str(), preview, ImGuiComboFlags_NoArrowButton);
            ImGui::PopStyleVar();

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImVec2 center(min.x + 9.0f, (min.y + max.y) * .5f);
            const ImU32 arrow = IM_COL32(128, 128, 128, 255);
            if (opened) {
                draw->AddTriangleFilled(
                    ImVec2(center.x - 4.0f, center.y + 2.0f),
                    ImVec2(center.x + 4.0f, center.y + 2.0f),
                    ImVec2(center.x, center.y - 3.0f), arrow);
            } else {
                draw->AddTriangleFilled(
                    ImVec2(center.x - 4.0f, center.y - 2.0f),
                    ImVec2(center.x + 4.0f, center.y - 2.0f),
                    ImVec2(center.x, center.y + 3.0f), arrow);
            }
            hovered = ImGui::IsItemHovered();
            held = ImGui::IsItemActive();
        } else {
            opened = ImGui::BeginCombo(id.c_str(), preview);
        }
    }
    if (opened) {
        for (int i = 0; i < static_cast<int>(stateData.options.size()); ++i) {
            ImGui::PushID(i);
            const bool pressed = Config::get().customMode
                ? custom_dropdown_option(stateData.options[i], selected == i)
                : ImGui::Selectable(stateData.options[i].c_str(), selected == i);
            ImGui::PopID();
            if (pressed) {
                selected = i;
                stateData.selectedIndex = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (Config::get().skeetMode || Config::get().customMode)
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
    bool hovered = false;
    bool held = false;
#ifdef GRAPE_PC
    if (Config::get().skeetMode) {
        pressed = grape::pc::skrt::colorButton(label, preview);
        hovered = ImGui::IsItemHovered();
        held = ImGui::IsItemActive();
    } else if (Config::get().customMode) {
        const float side = ImGui::GetFrameHeight();
        const auto shown = visible_label(label);
        const bool hideLabel = shown == "Color";
        align_custom_value(hideLabel ? std::string_view{} : shown);
        const ImVec2 right = ImGui::GetCursorScreenPos();
        const ImVec2 min(right.x + ImGui::CalcItemWidth() - side, right.y);
        ImGui::SetCursorScreenPos(min);
        pressed = ImGui::InvisibleButton(
            ("##color_button" + id).c_str(), ImVec2(side, side));
        hovered = ImGui::IsItemHovered();
        held = ImGui::IsItemActive();
        const float hover = animate_last_item(hovered ? 1.0f : 0.0f);
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 max(min.x + side, min.y + side);
        draw->AddRectFilled(min, max, ImGui::GetColorU32(preview), 4.0f);
        if (hover > .001f)
            draw->AddRectFilled(
                min, max,
                IM_COL32(255, 255, 255, static_cast<int>(30.0f * hover)),
                4.0f);
        draw->AddRect(min, max, IM_COL32(34, 34, 34, 255), 4.0f);
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
        hovered = ImGui::IsItemHovered();
        held = ImGui::IsItemActive();
    }
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
            if (ImGui::Button(("Copy##color_" + id).c_str(),
                              ImVec2(0.0f, ImGui::GetFrameHeight()))) {
                syncHex();
                ImGui::SetClipboardText(value.hex.c_str());
            }
            ImGui::TableNextColumn();
            if (ImGui::Button(("Paste##color_" + id).c_str(),
                              ImVec2(0.0f, ImGui::GetFrameHeight()))) {
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
#ifdef GRAPE_PC
    if (Config::get().skeetMode && font) {
        if (grape::pc::skrt::setSectionTitle(value)) return;
    }
#endif
    auto& config = Config::get();
    if (config.customMode && config.customSectionWantsTitle) {
        config.customSectionWantsTitle = false;
        config.customSectionTitle.assign(value);
        return;
    }
    ScopedFont scoped(
        config.skeetMode && font
            ? config.skeetHeaderFont
            : config.skeetMode ? nullptr : font);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}
inline void same_line() { ImGui::SameLine(); }
inline void spacer(double size = 0.0) { ImGui::Dummy(ImVec2(0.0f, size)); }
inline void begin_custom_section() {
    auto& config = Config::get();
    const int index = config.customSectionIndex++;
    if (index == 0) {
        const ImVec2 parentPos = ImGui::GetWindowPos();
        const ImVec2 parentSize = ImGui::GetWindowSize();
        config.customSectionClipMin = parentPos;
        config.customSectionClipMax = ImVec2(
            parentPos.x + parentSize.x, parentPos.y + parentSize.y);
        config.customSectionOrigin = ImGui::GetCursorScreenPos();
        config.customSectionOrigin.y += ImGui::GetFontSize() * .5f + 4.0f;
        config.customSectionWidth =
            (ImGui::GetContentRegionAvail().x -
             ImGui::GetStyle().ItemSpacing.x) * .5f;
        config.customSectionColumnY.fill(config.customSectionOrigin.y);
    }
    config.customSectionColumn = index & 1;
    config.customSectionMin = ImVec2(
        config.customSectionOrigin.x + config.customSectionColumn *
            (config.customSectionWidth + ImGui::GetStyle().ItemSpacing.x),
        config.customSectionColumnY[config.customSectionColumn]);
    ImGui::SetCursorScreenPos(config.customSectionMin);
    config.customSectionTitle.clear();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 28, 28, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(34, 34, 34, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 10.0f));
    const std::string id = "##default-section-" + std::to_string(index);
    const bool fixedHeight = config.customSectionHeight > 0.0f;
    ImGui::BeginChild(
        id.c_str(), ImVec2(config.customSectionWidth,
                           fixedHeight ? config.customSectionHeight : 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
            (fixedHeight ? 0 : ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysAutoResize),
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);
    config.customSectionStarted = true;
    config.customSectionWantsTitle = true;
}
inline void end_custom_section() {
    auto& config = Config::get();
    if (!config.customSectionStarted) return;
    if (!config.customSectionTitle.empty()) {
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(config.customSectionClipMin,
                           config.customSectionClipMax, false);
        const ImVec2 size = ImGui::CalcTextSize(config.customSectionTitle.c_str());
        const ImVec2 textPos(config.customSectionMin.x + 10.0f,
                             config.customSectionMin.y - size.y * .5f);
        draw->AddRectFilled(
            ImVec2(textPos.x - 4.0f, textPos.y),
            ImVec2(textPos.x + size.x + 4.0f, textPos.y + size.y),
            IM_COL32(28, 28, 28, 255));
        draw->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text),
                      config.customSectionTitle.c_str());
        draw->PopClipRect();
    }
    ImGui::EndChild();
    config.customSectionColumnY[config.customSectionColumn] =
        ImGui::GetItemRectMax().y + ImGui::GetFontSize() * .5f + 8.0f;
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    config.customSectionStarted = false;
    config.customSectionWantsTitle = false;
    config.customSectionTitle.clear();
}
inline void divider(bool visible = true) {
    (void)visible;
    auto& config = Config::get();
    if (config.customMode) {
        end_custom_section();
        begin_custom_section();
        return;
    }
    if (!config.skeetMode) {
        ImGui::Separator();
        return;
    }
#ifdef GRAPE_PC
    if (!config.skeetGridStarted && !visible) {
        config.skeetGridStarted = grape::pc::skrt::beginSections();
    } else if (config.skeetGridStarted && visible) {
        grape::pc::skrt::nextSection();
    }
#endif
}
inline void hide_section_box() {
#ifdef GRAPE_PC
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
    if (config.customMode) {
        config.customSectionStarted = false;
        config.customSectionWantsTitle = false;
        config.customSectionIndex = 0;
        config.customSectionWidth = 0.0f;
        config.customSectionColumn = 0;
        config.customSectionOrigin = {};
        config.customSectionColumnY = {};
        config.customSectionHeight = 0.0f;
        config.customSectionTitle.clear();
    }
    ImGui::PushID(static_cast<int>(expected));
    fn();
    if (config.customMode) {
        end_custom_section();
        ImGui::SetCursorScreenPos(ImVec2(
            config.customSectionOrigin.x,
            std::max(config.customSectionColumnY[0],
                     config.customSectionColumnY[1])));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::PopID();
    if (config.skeetMode && config.skeetGridStarted) {
#ifdef GRAPE_PC
        grape::pc::skrt::endSections();
#endif
        config.skeetGridStarted = false;
    }
}
template <class F>
void window(ImTextureID logoTex, ImVec2 logoSize, ImVec2 logoUv, F&& fn) {
    const float scale = Config::get().uiScale;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
#ifdef GRAPE_PC
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
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 titleMin = Config::get().customMode
            ? windowPos : titlePos;
        const ImVec2 titleMax(
            Config::get().customMode ? windowPos.x + windowSize.x
                                     : titlePos.x +
                                           ImGui::GetContentRegionAvail().x,
            titlePos.y + titleHeight);
        draw->AddRectFilled(titleMin, titleMax,
                            ImGui::GetColorU32(ImGuiCol_TitleBgActive),
                            ImGui::GetStyle().WindowRounding,
                            ImDrawFlags_RoundCornersTop);
        if (Config::get().customMode) {
            const char* username = Config::get().username;
            const ImVec2 usernameSize = ImGui::CalcTextSize(username);
            const float textY = titlePos.y +
                (titleHeight - ImGui::GetFontSize()) * .5f;
            static constexpr std::array quotes{
                "~ 'one frame at a time'", "~ 'stay precise'",
                "~ 'trust the replay'", "~ 'keep moving forward'",
                "~ 'practice makes progress'"};
            static const size_t quoteIndex = static_cast<size_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()) %
                quotes.size();
            draw->AddText(ImVec2(titleMin.x + 10.0f, textY),
                          IM_COL32(204, 204, 204, 145), quotes[quoteIndex]);
            draw->AddText(ImVec2(titleMax.x - usernameSize.x - 10.0f, textY),
                          IM_COL32(204, 204, 204, 255), username);
            draw->AddLine(
                ImVec2(titleMin.x, titleMax.y), titleMax,
                IM_COL32(34, 34, 34, 255), 1.0f);
            draw->AddRect(
                windowPos,
                ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
                IM_COL32(34, 34, 34, 255), ImGui::GetStyle().WindowRounding,
                ImDrawFlags_RoundCornersAll, 1.0f);
        } else if (logoTex) {
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
        if (!Config::get().customMode) {
            constexpr std::string_view credit = "Silicate \xe2\x99\xa5";
            const ImVec2 creditSize = ImGui::CalcTextSize(
                credit.data(), credit.data() + credit.size());
            ImVec4 creditColor = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
            draw->AddText(ImVec2(titleMax.x - creditSize.x - 10.0f,
                                 titlePos.y +
                                     (titleHeight - creditSize.y) * 0.5f),
                          ImGui::GetColorU32(creditColor), credit.data(),
                          credit.data() + credit.size());
        }
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
