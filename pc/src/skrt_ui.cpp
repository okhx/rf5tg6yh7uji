#include "skrt_ui.hpp"

#include <Geode/Geode.hpp>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>

#include "config/config.hpp"

namespace grape::pc::skrt {
namespace {
ImFont* s_text;
ImFont* s_bold;
ImFont* s_icons;

struct SectionLayout {
    bool active;
    bool groupOpen;
    int column;
    ImVec2 boxMin;
    std::string title;
    ImGuiTable* table;
    bool drawBox;
    ImVec2 origin;
    float nextY[2];
} s_sections;

bool s_fullWidthInput;

std::string_view visibleLabel(std::string_view label) {
    return label.substr(0, label.find("##"));
}

std::pair<float, float> contentBounds() {
    auto* table = ImGui::GetCurrentTable();
    if (table && table != s_sections.table && table->CurrentColumn >= 0) {
        const auto& column = table->Columns[table->CurrentColumn];
        if (s_sections.groupOpen) {
            return {
                std::max(column.WorkMinX, s_sections.boxMin.x + 19.0f),
                std::min(column.WorkMaxX, s_sections.boxMin.x + 239.0f)};
        }
        return {column.WorkMinX, column.WorkMaxX};
    }
    if (s_sections.groupOpen)
        return {s_sections.boxMin.x + 19.0f,
                s_sections.boxMin.x + 239.0f};
    const auto cursor = ImGui::GetCursorScreenPos();
    return {cursor.x, cursor.x + ImGui::GetContentRegionAvail().x};
}

void place(float width, bool left = false) {
    const auto [minimum, maximum] = contentBounds();
    const auto cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(
        left ? minimum : minimum + std::max(0.0f, maximum - minimum - width) * .5f,
        cursor.y));
}

void gradientBox(ImDrawList* draw, ImVec2 min, ImVec2 max, int top,
                 int bottom) {
    draw->AddRectFilledMultiColor(
        min, max, IM_COL32(top, top, top, 255), IM_COL32(top, top, top, 255),
        IM_COL32(bottom, bottom, bottom, 255),
        IM_COL32(bottom, bottom, bottom, 255));
    draw->AddRect(min, max, IM_COL32(10, 10, 10, 255));
}

float centeredPadding(float width, std::string_view text) {
    return std::max(4.0f, (width - ImGui::CalcTextSize(
        text.data(), text.data() + text.size()).x) * .5f);
}

void openSection() {
    const float x = s_sections.origin.x +
                    (s_sections.column == 0 ? 19.0f : 287.0f);
    s_sections.boxMin = ImVec2(x, s_sections.nextY[s_sections.column]);
    ImGui::SetCursorScreenPos(
        ImVec2(s_sections.boxMin.x + 19.0f, s_sections.boxMin.y + 22.0f));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(158.0f);
    s_sections.title.clear();
    s_sections.drawBox = true;
    s_sections.groupOpen = true;
}

void closeSection() {
    if (!s_sections.groupOpen) return;
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    const float bottom = std::max(
        ImGui::GetItemRectMax().y + 12.0f, s_sections.boxMin.y + 54.0f);
    const ImVec2 boxMax(s_sections.boxMin.x + 258.0f, bottom);
    auto* draw = ImGui::GetWindowDrawList();
    if (s_sections.drawBox) {
        draw->AddRect(s_sections.boxMin, boxMax, IM_COL32(10, 10, 10, 255));
        draw->AddRect(
            ImVec2(s_sections.boxMin.x + 1, s_sections.boxMin.y + 1),
            ImVec2(boxMax.x - 1, boxMax.y - 1),
            IM_COL32(48, 48, 48, 255));
    }
    if (s_sections.drawBox && !s_sections.title.empty()) {
        if (s_text) ImGui::PushFont(s_text);
        const ImVec2 textSize = ImGui::CalcTextSize(
            s_sections.title.c_str());
        const ImVec2 titleMin(s_sections.boxMin.x + 9.0f,
                              s_sections.boxMin.y - textSize.y * 0.5f);
        draw->AddRectFilled(
            ImVec2(titleMin.x - 3.0f, titleMin.y),
            ImVec2(titleMin.x + textSize.x + 3.0f,
                   titleMin.y + textSize.y),
            IM_COL32(17, 17, 17, 255));
        draw->AddText(titleMin, IM_COL32(213, 213, 213, 255),
                      s_sections.title.c_str());
        if (s_text) ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos(
        ImVec2(s_sections.boxMin.x, boxMax.y + 9.0f));
    ImGui::Dummy(ImVec2(258.0f, 1.0f));
    s_sections.nextY[s_sections.column] = boxMax.y + 9.0f;
    s_sections.groupOpen = false;
}

void clampScalar(ImGuiDataType type, void* value, const void* minimum,
                 const void* maximum) {
    switch (type) {
        case ImGuiDataType_S32:
            *static_cast<int*>(value) = std::clamp(
                *static_cast<int*>(value), *static_cast<const int*>(minimum),
                *static_cast<const int*>(maximum));
            break;
        case ImGuiDataType_U32:
            *static_cast<unsigned*>(value) = std::clamp(
                *static_cast<unsigned*>(value),
                *static_cast<const unsigned*>(minimum),
                *static_cast<const unsigned*>(maximum));
            break;
        case ImGuiDataType_S64:
            *static_cast<int64_t*>(value) = std::clamp(
                *static_cast<int64_t*>(value),
                *static_cast<const int64_t*>(minimum),
                *static_cast<const int64_t*>(maximum));
            break;
        case ImGuiDataType_U64:
            *static_cast<uint64_t*>(value) = std::clamp(
                *static_cast<uint64_t*>(value),
                *static_cast<const uint64_t*>(minimum),
                *static_cast<const uint64_t*>(maximum));
            break;
        case ImGuiDataType_Float:
            *static_cast<float*>(value) = std::clamp(
                *static_cast<float*>(value),
                *static_cast<const float*>(minimum),
                *static_cast<const float*>(maximum));
            break;
        case ImGuiDataType_Double:
            *static_cast<double*>(value) = std::clamp(
                *static_cast<double*>(value),
                *static_cast<const double*>(minimum),
                *static_cast<const double*>(maximum));
            break;
        default:
            break;
    }
}

double scalarValue(ImGuiDataType type, const void* value) {
    switch (type) {
        case ImGuiDataType_S32: return *static_cast<const int*>(value);
        case ImGuiDataType_U32: return *static_cast<const unsigned*>(value);
        case ImGuiDataType_S64: return static_cast<double>(*static_cast<const int64_t*>(value));
        case ImGuiDataType_U64: return static_cast<double>(*static_cast<const uint64_t*>(value));
        case ImGuiDataType_Float: return *static_cast<const float*>(value);
        case ImGuiDataType_Double: return *static_cast<const double*>(value);
        default: return 0.0;
    }
}

void setScalarValue(ImGuiDataType type, void* value, double next) {
    switch (type) {
        case ImGuiDataType_S32: *static_cast<int*>(value) = static_cast<int>(std::round(next)); break;
        case ImGuiDataType_U32: *static_cast<unsigned*>(value) = static_cast<unsigned>(std::round(next)); break;
        case ImGuiDataType_S64: *static_cast<int64_t*>(value) = static_cast<int64_t>(std::round(next)); break;
        case ImGuiDataType_U64: *static_cast<uint64_t*>(value) = static_cast<uint64_t>(std::round(next)); break;
        case ImGuiDataType_Float: *static_cast<float*>(value) = static_cast<float>(next); break;
        case ImGuiDataType_Double: *static_cast<double*>(value) = next; break;
        default: break;
    }
}

}

void setup() {
    auto* atlas = ImGui::GetIO().Fonts;
    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 3;
    config.PixelSnapH = true;
    const auto resources = geode::Mod::get()->getResourcesDir();
    s_text = atlas->AddFontFromFileTTF(
        geode::utils::string::pathToString(resources / "font_main.ttf").c_str(),
        11.0f, &config, atlas->GetGlyphRangesCyrillic());
    s_bold = atlas->AddFontFromFileTTF(
        geode::utils::string::pathToString(resources / "font_bold.ttf").c_str(),
        11.0f, &config, atlas->GetGlyphRangesCyrillic());
    static ImVector<ImWchar> iconRanges;
    ImFontGlyphRangesBuilder icons;
    for (ImWchar icon : {ImWchar(0xf192), ImWchar(0xf06e), ImWchar(0xf03d),
                         ImWchar(0xf121), ImWchar(0xf013), ImWchar(0xf044),
                         ImWchar(0xf004)})
        icons.AddChar(icon);
    icons.BuildRanges(&iconRanges);
    s_icons = atlas->AddFontFromFileTTF(
        geode::utils::string::pathToString(
            geode::Mod::get()->getResourcesDir() / "font_symbols.ttf").c_str(),
        30.0f, &config, iconRanges.Data);
}

ImFont* headingFont() {
    return s_bold;
}

void pushStyle(float opacity) {
    const auto c = [opacity](int r, int g, int b, int a = 255) {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f,
                      a / 255.0f * opacity);
    };
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, c(213, 213, 213));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, c(125, 125, 125));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, c(17, 17, 17, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, c(17, 17, 17, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, c(40, 40, 40));
    ImGui::PushStyleColor(ImGuiCol_Border, c(10, 10, 10));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c(32, 32, 38));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, c(42, 42, 48));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, c(48, 48, 54));
    ImGui::PushStyleColor(ImGuiCol_Button, c(31, 31, 31));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c(41, 41, 41));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c(51, 51, 51));
    const auto& accent = GrapeSettings::get()->skeetAccent;
    const auto ac = [&](float factor) {
        return ImVec4(accent[0] * factor, accent[1] * factor,
                      accent[2] * factor, accent[3] * opacity);
    };
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ac(1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ac(1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ac(1.08f));
    ImGui::PushStyleColor(ImGuiCol_Header, ac(0.46f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ac(0.60f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ac(0.74f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ac(0.55f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, c(45, 45, 45));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, c(65, 65, 65));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, c(75, 75, 75));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, c(75, 75, 75));
    ImGui::PushStyleColor(ImGuiCol_Separator, c(10, 10, 10));
    if (s_text) ImGui::PushFont(s_text);
}

void popStyle() {
    if (s_text) ImGui::PopFont();
    ImGui::PopStyleColor(24);
    ImGui::PopStyleVar(12);
}

bool beginWindow() {
    ImGui::SetNextWindowSize(ImVec2(660, 560), ImGuiCond_Always);
    const bool visible = ImGui::Begin(
        "Main UI", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    if (!visible) return false;
    const ImVec2 pos = ImGui::GetWindowPos();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 innerMin(pos.x + 6, pos.y + 6);
    const ImVec2 innerMax(innerMin.x + 648, innerMin.y + 548);
    draw->AddRectFilled(pos, ImVec2(pos.x + 660, pos.y + 560),
                        IM_COL32(17, 17, 17, 255));
    draw->AddRectFilled(innerMin, innerMax, IM_COL32(17, 17, 17, 255));
    draw->AddRect(pos, ImVec2(pos.x + 660, pos.y + 560),
                  IM_COL32(8, 8, 8, 255));
    draw->AddRect(ImVec2(pos.x + 1, pos.y + 1),
                  ImVec2(pos.x + 659, pos.y + 559),
                  IM_COL32(69, 69, 69, 255));
    draw->AddRect(ImVec2(pos.x + 4, pos.y + 4),
                  ImVec2(pos.x + 656, pos.y + 556),
                  IM_COL32(69, 69, 69, 255));
    draw->AddRect(ImVec2(pos.x + 5, pos.y + 5),
                  ImVec2(pos.x + 655, pos.y + 555),
                  IM_COL32(8, 8, 8, 255));
    draw->AddRect(innerMin, innerMax, IM_COL32(10, 10, 10, 255));
    const auto color = [](const std::array<float, 4>& value) {
        return ImGui::ColorConvertFloat4ToU32(
            {value[0], value[1], value[2], value[3]});
    };
    const auto* settings = GrapeSettings::get();
    const ImVec2 middle(innerMin.x + 324, innerMin.y + 2);
    draw->AddRectFilledMultiColor(
        innerMin, middle, color(settings->skeetGradientLeft),
        color(settings->skeetGradientMiddle),
        color(settings->skeetGradientMiddle),
        color(settings->skeetGradientLeft));
    draw->AddRectFilledMultiColor(
        ImVec2(middle.x, innerMin.y), ImVec2(innerMax.x, innerMin.y + 2),
        color(settings->skeetGradientMiddle),
        color(settings->skeetGradientRight),
        color(settings->skeetGradientRight),
        color(settings->skeetGradientMiddle));
    draw->AddRectFilled(innerMin, ImVec2(innerMax.x, innerMin.y + 2),
                        IM_COL32(0, 0, 0, 110));
    ImGui::SetCursorPos(ImVec2(6, 6));
    ImGui::InvisibleButton("##skrt-drag", ImVec2(648, 10));
    if (ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const auto delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
    }
    ImGui::SetCursorPos(ImVec2(6, 8));
    return true;
}

void endWindow() {
    ImGui::End();
}

bool tab(const char* id, const char* icon, bool selected) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(75, 75);
    const bool pressed = ImGui::InvisibleButton(id, size);
    const ImVec2 max(min.x + size.x, min.y + size.y);
    auto* draw = ImGui::GetWindowDrawList();
    if (selected) {
        draw->AddRectFilled(min, max, IM_COL32(17, 17, 17, 225));
        draw->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, min.y),
                      IM_COL32(48, 48, 48, 255));
        draw->AddLine(ImVec2(min.x, max.y - 1),
                      ImVec2(max.x, max.y - 1),
                      IM_COL32(48, 48, 48, 255));
        draw->AddLine(ImVec2(max.x - 1, min.y),
                      ImVec2(max.x - 1, max.y),
                      IM_COL32(10, 10, 10, 255));
    }
    const ImU32 tint = selected || ImGui::IsItemHovered()
        ? IM_COL32(185, 185, 185, 255)
        : IM_COL32(100, 100, 100, 255);
    if (std::strcmp(id, "Scripts") == 0 && s_bold) {
        const auto iconSize = s_bold->CalcTextSizeA(15, FLT_MAX, 0, "LUA");
        draw->AddText(s_bold, 15,
                      ImVec2(min.x + (75 - iconSize.x) * .5f,
                             min.y + (75 - iconSize.y) * .5f),
                      tint, "LUA");
    } else if (s_icons) {
        const auto iconSize =
            s_icons->CalcTextSizeA(30, FLT_MAX, 0, icon);
        draw->AddText(s_icons, 30,
                      ImVec2(min.x + (75 - iconSize.x) * .5f,
                             min.y + (75 - iconSize.y) * .5f),
                      tint, icon);
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + 75));
    return pressed;
}

bool button(std::string_view label, ImVec2 requested) {
    const auto shown = visibleLabel(label);
    const auto [left, right] = contentBounds();
    const float available = right - left;
    const float width = requested.x < 0
        ? available
        : requested.x > 0 ? std::min(requested.x, available)
                          : std::min(158.0f, available);
    const ImVec2 size(width, requested.y > 0 ? requested.y : 20.0f);
    if (requested.x >= 0) place(width);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const std::string id(label);
    const bool pressed = ImGui::InvisibleButton(id.c_str(), size);
    gradientBox(ImGui::GetWindowDrawList(), min,
                ImVec2(min.x + size.x, min.y + size.y),
                ImGui::IsItemHovered() ? 41 : 31,
                ImGui::IsItemHovered() ? 46 : 36);
    const auto textSize =
        ImGui::CalcTextSize(shown.data(), shown.data() + shown.size());
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(min.x + (size.x - textSize.x) * .5f,
               min.y + (size.y - textSize.y) * .5f),
        IM_COL32(213, 213, 213, 255), shown.data(),
        shown.data() + shown.size());
    return pressed;
}

bool checkbox(std::string_view label, bool& value) {
    const auto shown = visibleLabel(label);
    const ImVec2 textSize =
        ImGui::CalcTextSize(shown.data(), shown.data() + shown.size());
    place(textSize.x + 21, true);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const std::string id(label);
    const bool pressed = ImGui::InvisibleButton(
        id.c_str(), ImVec2(textSize.x + 21, std::max(16.0f, textSize.y)));
    if (pressed) value = !value;
    const ImVec2 boxMin(min.x + 1, min.y + 1);
    const ImVec2 boxMax(boxMin.x + 13, boxMin.y + 13);
    const auto& accent = GrapeSettings::get()->skeetAccent;
    const auto tint = [&](float factor) {
        return ImGui::ColorConvertFloat4ToU32(
            {accent[0] * factor, accent[1] * factor,
             accent[2] * factor, accent[3]});
    };
    const int top = ImGui::IsItemHovered() ? 86 : 76;
    const int bottom = ImGui::IsItemHovered() ? 61 : 51;
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        boxMin, boxMax, value ? tint(1.0f) : IM_COL32(top, top, top, 255),
        value ? tint(1.0f) : IM_COL32(top, top, top, 255),
        value ? tint(0.52f) : IM_COL32(bottom, bottom, bottom, 255),
        value ? tint(0.52f) : IM_COL32(bottom, bottom, bottom, 255));
    ImGui::GetWindowDrawList()->AddRect(
        boxMin, boxMax, IM_COL32(10, 10, 10, 255));
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(min.x + 19, min.y + 1), IM_COL32(213, 213, 213, 255),
        shown.data(), shown.data() + shown.size());
    return pressed;
}

bool dragScalar(std::string_view label, ImGuiDataType type, void* value,
                const void* minimum, const void* maximum, float speed) {
    const auto shown = visibleLabel(label);
    const std::string id = "##skrt-input-number-" + std::string(label);
    const auto [left, right] = contentBounds();
    const ImVec2 row = ImGui::GetCursorScreenPos();
    const float width = std::min(shown.empty() ? 158.0f : 120.0f,
                                 right - left);
    if (!shown.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(left, row.y + 1));
        ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
        ImGui::SetCursorScreenPos(ImVec2(right - width, row.y));
    } else {
        place(width);
    }
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + ImGui::GetFontSize() + 6);
    gradientBox(ImGui::GetWindowDrawList(), min, max, 31, 36);
    ImGui::SetNextItemWidth(width);
    const char* format = type == ImGuiDataType_Float ||
                                 type == ImGuiDataType_Double
                             ? "%.6g"
                             : ImGui::DataTypeGetInfo(type)->PrintFmt;
    char formatted[64]{};
    ImGui::DataTypeFormatString(formatted, sizeof(formatted), type, value,
                                format);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(centeredPadding(width, formatted), 3));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    const bool changed = ImGui::InputScalar(
        id.c_str(), type, value, nullptr, nullptr, format,
        ImGuiInputTextFlags_AutoSelectAll |
            ImGuiInputTextFlags_NoHorizontalScroll);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (changed) clampScalar(type, value, minimum, maximum);
    static_cast<void>(speed);
    return changed;
}

bool sliderScalar(std::string_view label, ImGuiDataType type, void* value,
                  const void* minimum, const void* maximum) {
    const auto shown = visibleLabel(label);
    const auto [left, right] = contentBounds();
    const float width = std::min(158.0f, right - left);
    if (!shown.empty()) {
        const auto row = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(left, row.y));
        ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
    }
    place(width);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + 9.0f);
    const std::string id = "##skrt-slider-" + std::string(label);
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, 9.0f));
    const double low = scalarValue(type, minimum);
    const double high = scalarValue(type, maximum);
    const double before = scalarValue(type, value);
    if (high > low && ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        const double ratio = std::clamp(
            (ImGui::GetIO().MousePos.x - min.x) / width, 0.0f, 1.0f);
        setScalarValue(type, value, low + (high - low) * ratio);
        clampScalar(type, value, minimum, maximum);
    }
    const double current = scalarValue(type, value);
    const float ratio = high > low
        ? static_cast<float>(std::clamp((current - low) / (high - low), 0.0, 1.0))
        : 0.0f;
    auto* draw = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    gradientBox(draw, min, max, hovered ? 62 : 52, hovered ? 78 : 68);
    const auto& accent = GrapeSettings::get()->skeetAccent;
    const ImU32 top = ImGui::ColorConvertFloat4ToU32(
        {accent[0], accent[1], accent[2], accent[3]});
    const ImU32 bottom = ImGui::ColorConvertFloat4ToU32(
        {accent[0] * .55f, accent[1] * .55f, accent[2] * .55f, accent[3]});
    const ImVec2 fillMax(min.x + width * ratio, max.y);
    if (fillMax.x > min.x)
        draw->AddRectFilledMultiColor(min, fillMax, top, top, bottom, bottom);
    draw->AddRect(min, max, IM_COL32(10, 10, 10, 255));
    char formatted[64]{};
    const char* format = type == ImGuiDataType_Float || type == ImGuiDataType_Double
        ? "%.3g" : ImGui::DataTypeGetInfo(type)->PrintFmt;
    ImGui::DataTypeFormatString(formatted, sizeof(formatted), type, value, format);
    const auto textSize = ImGui::CalcTextSize(formatted);
    const ImVec2 textPos(min.x + (width - textSize.x) * .5f,
                         min.y + (9.0f - textSize.y) * .5f);
    draw->AddText(ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 0, 0, 220), formatted);
    draw->AddText(textPos, IM_COL32(235, 235, 235, 255), formatted);
    return current != before;
}

bool tripleSlider(std::string_view label, float& a, float& b, float& c) {
    float sum = a + b + c;
    if (!(sum > 0.0f)) {
        a = b = c = 100.0f / 3.0f;
        sum = 100.0f;
    } else if (sum < 99.999f || sum > 100.001f) {
        a = a / sum * 100.0f;
        b = b / sum * 100.0f;
        c = c / sum * 100.0f;
    }

    const auto shown = visibleLabel(label);
    const auto [left, right] = contentBounds();
    const float width = std::min(158.0f, right - left);
    if (!shown.empty()) {
        const auto row = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(left, row.y));
        ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
    }
    place(width);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + 9.0f);
    const std::string id = "##skrt-triple-" + std::string(label);
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, 9.0f));

    float d1 = a * 0.01f;
    float d2 = (a + b) * 0.01f;

    static ImGuiID s_activeId = 0;
    static int s_activeDivider = 0;
    bool changed = false;
    if (width > 0.0f && ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        const float ratio = std::clamp(
            (ImGui::GetIO().MousePos.x - min.x) / width, 0.0f, 1.0f);
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
    const float mid1 = min.x + width * d1;
    const float mid2 = min.x + width * d2;
    const ImU32 colA = IM_COL32(70, 150, 95, 255);
    const ImU32 colB = IM_COL32(70, 115, 175, 255);
    const ImU32 colC = IM_COL32(180, 130, 65, 255);
    draw->AddRectFilled(min, ImVec2(mid1, max.y), colA);
    draw->AddRectFilled(ImVec2(mid1, min.y), ImVec2(mid2, max.y), colB);
    draw->AddRectFilled(ImVec2(mid2, min.y), max, colC);
    draw->AddLine(ImVec2(mid1, min.y), ImVec2(mid1, max.y),
                  IM_COL32(10, 10, 10, 255), 2.0f);
    draw->AddLine(ImVec2(mid2, min.y), ImVec2(mid2, max.y),
                  IM_COL32(10, 10, 10, 255), 2.0f);
    draw->AddRect(min, max, IM_COL32(10, 10, 10, 255));

    const auto segLabel = [&](float loX, float hiX, float pct) {
        char buf[16]{};
        std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        if (hiX - loX < ts.x + 2.0f) return;
        const ImVec2 tp((loX + hiX - ts.x) * 0.5f,
                        min.y + (9.0f - ts.y) * 0.5f);
        draw->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 220), buf);
        draw->AddText(tp, IM_COL32(240, 240, 240, 255), buf);
    };
    segLabel(min.x, mid1, a);
    segLabel(mid1, mid2, b);
    segLabel(mid2, max.x, c);
    return changed;
}

bool inputText(std::string_view label, std::string_view hint,
               std::string& value) {
    const auto shown = visibleLabel(label);
    const auto [left, right] = contentBounds();
    const float width = s_fullWidthInput
        ? right - left
        : std::min(shown.empty() ? 158.0f : 120.0f, right - left);
    const ImVec2 row = ImGui::GetCursorScreenPos();
    if (!shown.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(left, row.y + 1));
        ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
        ImGui::SetCursorScreenPos(ImVec2(right - width, row.y));
    } else {
        place(width);
    }
    s_fullWidthInput = false;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + ImGui::GetFontSize() + 6);
    gradientBox(ImGui::GetWindowDrawList(), min, max, 31, 36);
    ImGui::SetNextItemWidth(width);
    const std::string id = "##skrt-input-" + std::string(label);
    const std::string_view displayed = value.empty() ? hint : value;
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(centeredPadding(width, displayed), 3));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    const bool changed = ImGui::InputTextWithHint(
        id.c_str(), std::string(hint).c_str(), &value,
        ImGuiInputTextFlags_AutoSelectAll |
            ImGuiInputTextFlags_NoHorizontalScroll);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return changed;
}

void nextInputFullWidth() {
    s_fullWidthInput = true;
}

bool beginCombo(std::string_view label, const char* preview) {
    const auto shown = visibleLabel(label);
    const auto [left, right] = contentBounds();
    const ImVec2 row = ImGui::GetCursorScreenPos();
    const float width = std::min(shown.empty() ? 158.0f : 120.0f,
                                 right - left);
    if (!shown.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(left, row.y));
        ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
        ImGui::SetCursorScreenPos(ImVec2(right - width, row.y));
    } else {
        place(width);
    }
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + ImGui::GetFontSize() + 6);
    auto* parentDraw = ImGui::GetWindowDrawList();
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    const std::string id = "##skrt-combo-" + std::string(label);
    const bool opened =
        ImGui::BeginCombo(id.c_str(), "", ImGuiComboFlags_NoArrowButton);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    gradientBox(parentDraw, min, max, 31, 36);
    const auto textSize = ImGui::CalcTextSize(preview);
    parentDraw->AddText(
        ImVec2(min.x + std::max(4.0f, (width - textSize.x) * .5f),
               min.y + 3.0f),
        IM_COL32(213, 213, 213, 255), preview);
    if (s_icons) {
        parentDraw->AddText(
            s_icons, 10.0f, ImVec2(max.x - 14, min.y + 1),
            IM_COL32(162, 162, 162, 255), "\uf078");
    }
    return opened;
}

bool colorButton(std::string_view label, const ImVec4& value) {
    const auto shown = visibleLabel(label);
    const auto [left, right] = contentBounds();
    auto row = ImGui::GetCursorScreenPos();
    row.x = left;
    ImGui::SetCursorScreenPos(row);
    ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
    const ImVec2 min(right - 20.0f, row.y + 1.0f);
    ImGui::SetCursorScreenPos(min);
    const std::string id = "##skrt-color-" + std::string(label);
    const bool pressed = ImGui::InvisibleButton(id.c_str(), ImVec2(20, 11));
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, ImVec2(min.x + 20, min.y + 11),
                        ImGui::ColorConvertFloat4ToU32(value));
    draw->AddRect(min, ImVec2(min.x + 20, min.y + 11),
                  IM_COL32(10, 10, 10, 255));
    draw->AddRect(ImVec2(min.x + 1, min.y + 1),
                  ImVec2(min.x + 19, min.y + 10),
                  IM_COL32(68, 68, 68, 255));
    ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + 16));
    ImGui::Dummy(ImVec2(1, 1));
    return pressed;
}

bool beginSections() {
    if (s_sections.active) return true;
    s_sections.active = true;
    s_sections.origin = ImGui::GetCursorScreenPos();
    s_sections.nextY[0] = s_sections.origin.y + 9.0f;
    s_sections.nextY[1] = s_sections.origin.y + 9.0f;
    s_sections.column = 0;
    openSection();
    return true;
}

void nextSection() {
    if (!s_sections.active) return;
    closeSection();
    s_sections.column = 1 - s_sections.column;
    openSection();
}

void endSections() {
    if (!s_sections.active) return;
    closeSection();
    ImGui::SetCursorScreenPos(ImVec2(
        s_sections.origin.x,
        std::max(s_sections.nextY[0], s_sections.nextY[1])));
    ImGui::Dummy(ImVec2(554.0f, 1.0f));
    s_sections = {};
}

bool setSectionTitle(std::string_view title) {
    if (!s_sections.groupOpen) return false;
    s_sections.title.assign(title);
    return true;
}

void hideSectionBox() {
    s_sections.drawBox = false;
}

}
