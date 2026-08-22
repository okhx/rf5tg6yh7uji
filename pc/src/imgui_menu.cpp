#include "imgui_menu.hpp"

#include <Geode/Geode.hpp>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

namespace grape::pc::modern {
namespace {

struct SectionLayout {
    bool active = false;
    bool groupOpen = false;
    bool drawBox = true;
    int columns = 1;
    int column = 0;
    float width = 0.0f;
    ImVec2 origin{};
    ImVec2 boxMin{};
    std::array<float, 3> nextY{};
    std::string title;
    ImDrawListSplitter splitter;
} s_sections;

std::string_view visibleLabel(std::string_view label) {
    return label.substr(0, label.find("##"));
}

std::pair<float, float> contentBounds() {
    if (auto* table = ImGui::GetCurrentTable();
        table && table->CurrentColumn >= 0) {
        const auto& column = table->Columns[table->CurrentColumn];
        if (s_sections.groupOpen)
            return {std::max(column.WorkMinX, s_sections.boxMin.x + 18.0f),
                    std::min(column.WorkMaxX,
                             s_sections.boxMin.x + s_sections.width - 18.0f)};
        return {column.WorkMinX, column.WorkMaxX};
    }
    if (s_sections.groupOpen)
        return {s_sections.boxMin.x + 18.0f,
                s_sections.boxMin.x + s_sections.width - 18.0f};
    const auto cursor = ImGui::GetCursorScreenPos();
    return {cursor.x, cursor.x + ImGui::GetContentRegionAvail().x};
}

float controlWidth(std::string_view label) {
    const auto [left, right] = contentBounds();
    return std::min(150.0f, std::max(80.0f, (right - left) *
        (visibleLabel(label).empty() ? 1.0f : 0.46f)));
}

void placeRight(float width, float y) {
    const auto [left, right] = contentBounds();
    ImGui::SetCursorScreenPos(ImVec2(std::max(left, right - width), y));
}

void drawLabel(std::string_view label, float y) {
    const auto shown = visibleLabel(label);
    if (shown.empty()) return;
    const auto [left, right] = contentBounds();
    static_cast<void>(right);
    ImGui::SetCursorScreenPos(ImVec2(left, y + 5.0f));
    ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
}

ImVec4 accent(float alpha = 1.0f) {
    auto color = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    color.w *= alpha;
    return color;
}

void openSection() {
    s_sections.column = 0;
    for (int i = 1; i < s_sections.columns; ++i)
        if (s_sections.nextY[i] < s_sections.nextY[s_sections.column])
            s_sections.column = i;
    const float gap = 12.0f;
    s_sections.boxMin = ImVec2(
        s_sections.origin.x + s_sections.column * (s_sections.width + gap),
        s_sections.nextY[s_sections.column]);
    ImGui::SetCursorScreenPos(
        ImVec2(s_sections.boxMin.x + 18.0f, s_sections.boxMin.y + 34.0f));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(std::max(80.0f, s_sections.width - 36.0f));
    s_sections.title.clear();
    s_sections.drawBox = true;
    s_sections.groupOpen = true;
}

void closeSection() {
    if (!s_sections.groupOpen) return;
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    const float bottom = std::max(ImGui::GetItemRectMax().y + 15.0f,
                                  s_sections.boxMin.y + 64.0f);
    const ImVec2 boxMax(s_sections.boxMin.x + s_sections.width, bottom);
    auto* draw = ImGui::GetWindowDrawList();
    s_sections.splitter.SetCurrentChannel(draw, 0);
    if (s_sections.drawBox) {
        draw->AddRectFilled(s_sections.boxMin, boxMax,
                            IM_COL32(19, 19, 21, 248), 3.0f);
        draw->AddRect(s_sections.boxMin, boxMax,
                      IM_COL32(47, 47, 51, 210), 3.0f);
    }
    if (s_sections.drawBox && !s_sections.title.empty()) {
        draw->AddText(ImVec2(s_sections.boxMin.x + 18.0f,
                             s_sections.boxMin.y + 11.0f),
                      IM_COL32(148, 148, 155, 255), s_sections.title.c_str());
    }
    s_sections.splitter.SetCurrentChannel(draw, 1);
    s_sections.nextY[s_sections.column] = bottom + 12.0f;
    s_sections.groupOpen = false;
}

void clampScalar(ImGuiDataType type, void* value, const void* minimum,
                 const void* maximum) {
    switch (type) {
        case ImGuiDataType_S32:
            *static_cast<int*>(value) = std::clamp(*static_cast<int*>(value),
                *static_cast<const int*>(minimum), *static_cast<const int*>(maximum));
            break;
        case ImGuiDataType_U32:
            *static_cast<unsigned*>(value) = std::clamp(*static_cast<unsigned*>(value),
                *static_cast<const unsigned*>(minimum), *static_cast<const unsigned*>(maximum));
            break;
        case ImGuiDataType_S64:
            *static_cast<int64_t*>(value) = std::clamp(*static_cast<int64_t*>(value),
                *static_cast<const int64_t*>(minimum), *static_cast<const int64_t*>(maximum));
            break;
        case ImGuiDataType_U64:
            *static_cast<uint64_t*>(value) = std::clamp(*static_cast<uint64_t*>(value),
                *static_cast<const uint64_t*>(minimum), *static_cast<const uint64_t*>(maximum));
            break;
        case ImGuiDataType_Float:
            *static_cast<float*>(value) = std::clamp(*static_cast<float*>(value),
                *static_cast<const float*>(minimum), *static_cast<const float*>(maximum));
            break;
        case ImGuiDataType_Double:
            *static_cast<double*>(value) = std::clamp(*static_cast<double*>(value),
                *static_cast<const double*>(minimum), *static_cast<const double*>(maximum));
            break;
        default: break;
    }
}

}

void pushStyle(float opacity) {
    const auto currentAccent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const auto c = [opacity](int r, int g, int b, int a = 255) {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f,
                      a / 255.0f * opacity);
    };
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(11, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, c(239, 239, 242));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, c(139, 139, 146));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, c(11, 11, 13));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, c(11, 11, 13, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, c(22, 22, 25));
    ImGui::PushStyleColor(ImGuiCol_Border, c(43, 43, 48));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c(28, 28, 31));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, c(37, 37, 41));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, c(43, 43, 48));
    ImGui::PushStyleColor(ImGuiCol_Button, c(28, 28, 31));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c(39, 39, 43));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c(48, 48, 53));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,
        ImVec4(currentAccent.x, currentAccent.y, currentAccent.z,
               currentAccent.w * opacity));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, currentAccent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, currentAccent);
    ImGui::PushStyleColor(ImGuiCol_Header,
        ImVec4(currentAccent.x, currentAccent.y, currentAccent.z, .30f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
        ImVec4(currentAccent.x, currentAccent.y, currentAccent.z, .45f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
        ImVec4(currentAccent.x, currentAccent.y, currentAccent.z, .58f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg,
        ImVec4(currentAccent.x, currentAccent.y, currentAccent.z, .45f));
    ImGui::PushStyleColor(ImGuiCol_Separator, c(45, 45, 49));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, c(15, 15, 17));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, c(56, 56, 61));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, c(72, 72, 78));
}

void popStyle() {
    ImGui::PopStyleColor(23);
    ImGui::PopStyleVar(12);
}

bool beginWindow(ImTextureID logo, ImVec2 logoSize, ImVec2 logoUv) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(780, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(620, 460), ImVec2(viewport->WorkSize.x * .96f,
                                 viewport->WorkSize.y * .96f));
    const bool visible = ImGui::Begin(
        "Main UI", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);
    if (!visible) return false;
    const ImVec2 window = ImGui::GetWindowPos();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const float headerHeight = 44.0f;
    ImGui::InvisibleButton("##modern-drag",
                          ImVec2(ImGui::GetContentRegionAvail().x, headerHeight));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        const auto delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(window.x + delta.x, window.y + delta.y));
    }
    if (logo && logoSize.y > 0.0f) {
        const float height = 27.0f;
        const float width = std::min(92.0f, logoSize.x * height / logoSize.y);
        draw->AddImage(logo, ImVec2(cursor.x + 2, cursor.y + 5),
                       ImVec2(cursor.x + 2 + width, cursor.y + 5 + height),
                       ImVec2(0, 0), logoUv);
    } else {
        draw->AddText(ImVec2(cursor.x + 4, cursor.y + 10),
                      IM_COL32(240, 240, 244, 255), "Grape");
    }
    draw->AddLine(ImVec2(cursor.x, cursor.y + headerHeight),
                  ImVec2(cursor.x + ImGui::GetContentRegionAvail().x,
                         cursor.y + headerHeight),
                  IM_COL32(38, 38, 42, 255));
    return true;
}

void endWindow() { ImGui::End(); }

bool tab(const char* label, const char* icon, bool selected) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::CalcTextSize(label).x + 20.0f, 30.0f);
    const bool pressed = ImGui::InvisibleButton(label, size);
    auto* draw = ImGui::GetWindowDrawList();
    const auto ac = accent();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    if (selected)
        draw->AddRectFilled(min, max,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(ac.x * .22f, ac.y * .22f, ac.z * .22f, .95f)), 3.0f);
    else if (ImGui::IsItemHovered())
        draw->AddRectFilled(min, max, IM_COL32(29, 29, 32, 255), 3.0f);
    static_cast<void>(icon);
    std::string text(label);
    const auto textSize = ImGui::CalcTextSize(text.c_str());
    draw->AddText(ImVec2(min.x + (size.x - textSize.x) * .5f,
                         min.y + (size.y - textSize.y) * .5f),
                  selected ? IM_COL32(241, 241, 244, 255)
                           : IM_COL32(151, 151, 158, 255), text.c_str());
    return pressed;
}

bool button(std::string_view label, ImVec2 requested) {
    const float width = requested.x < 0 ? contentBounds().second - contentBounds().first
        : requested.x > 0 ? requested.x : controlWidth(label);
    const float y = ImGui::GetCursorScreenPos().y;
    if (requested.x >= 0) placeRight(width, y);
    return ImGui::Button(std::string(label).c_str(),
                         ImVec2(width, requested.y > 0 ? requested.y : 30.0f));
}

bool checkbox(std::string_view label, bool& value) {
    const float y = ImGui::GetCursorScreenPos().y;
    drawLabel(label, y);
    const ImVec2 size(46, 24);
    placeRight(size.x, y);
    const std::string id = "##modern-toggle-" + std::string(label);
    const bool pressed = ImGui::InvisibleButton(id.c_str(), size);
    if (pressed) value = !value;
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    draw->AddRectFilled(min, max, value ? ImGui::GetColorU32(accent())
                                       : IM_COL32(34, 34, 38, 255), 12.0f);
    const float cx = value ? max.x - 12.0f : min.x + 12.0f;
    draw->AddCircleFilled(ImVec2(cx, (min.y + max.y) * .5f), 8.0f,
                          IM_COL32(235, 235, 239, 255));
    return pressed;
}

bool dragScalar(std::string_view label, ImGuiDataType type, void* value,
                const void* minimum, const void* maximum, float speed) {
    const float y = ImGui::GetCursorScreenPos().y;
    drawLabel(label, y);
    const float width = controlWidth(label);
    placeRight(width, y);
    ImGui::SetNextItemWidth(width);
    const std::string id = "##modern-drag-" + std::string(label);
    const bool changed = ImGui::DragScalar(id.c_str(), type, value, speed,
                                           minimum, maximum);
    if (changed) clampScalar(type, value, minimum, maximum);
    return changed;
}

bool inputText(std::string_view label, std::string_view hint,
               std::string& value) {
    const float y = ImGui::GetCursorScreenPos().y;
    drawLabel(label, y);
    const float width = controlWidth(label);
    placeRight(width, y);
    ImGui::SetNextItemWidth(width);
    const std::string id = "##modern-input-" + std::string(label);
    return ImGui::InputTextWithHint(id.c_str(), std::string(hint).c_str(), &value);
}

bool beginCombo(std::string_view label, const char* preview) {
    const float y = ImGui::GetCursorScreenPos().y;
    drawLabel(label, y);
    const float width = controlWidth(label);
    placeRight(width, y);
    ImGui::SetNextItemWidth(width);
    const std::string id = "##modern-combo-" + std::string(label);
    return ImGui::BeginCombo(id.c_str(), preview);
}

bool colorButton(std::string_view label, const ImVec4& color) {
    const float y = ImGui::GetCursorScreenPos().y;
    drawLabel(label, y);
    const ImVec2 size(28, 18);
    placeRight(size.x, y + 3.0f);
    return ImGui::ColorButton(("##modern-color-" + std::string(label)).c_str(),
                              color, ImGuiColorEditFlags_AlphaPreviewHalf, size);
}

bool beginSections() {
    if (s_sections.active) return true;
    const float available = ImGui::GetContentRegionAvail().x;
    s_sections.active = true;
    s_sections.origin = ImGui::GetCursorScreenPos();
    s_sections.columns = available >= 590.0f ? 2 : 1;
    const float gap = 12.0f;
    s_sections.width = (available - gap * (s_sections.columns - 1)) /
                       s_sections.columns;
    s_sections.nextY.fill(s_sections.origin.y);
    s_sections.splitter.Split(ImGui::GetWindowDrawList(), 2);
    s_sections.splitter.SetCurrentChannel(ImGui::GetWindowDrawList(), 1);
    openSection();
    return true;
}

void nextSection() {
    if (!s_sections.active) return;
    closeSection();
    openSection();
}

void endSections() {
    if (!s_sections.active) return;
    closeSection();
    s_sections.splitter.Merge(ImGui::GetWindowDrawList());
    const float bottom = *std::max_element(s_sections.nextY.begin(),
        s_sections.nextY.begin() + s_sections.columns);
    ImGui::SetCursorScreenPos(ImVec2(s_sections.origin.x, bottom));
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 1.0f));
    s_sections = {};
}

bool setSectionTitle(std::string_view title) {
    if (!s_sections.groupOpen) return false;
    s_sections.title.assign(title);
    return true;
}

void hideSectionBox() { s_sections.drawBox = false; }

}
