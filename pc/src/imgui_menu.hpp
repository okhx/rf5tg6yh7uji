#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace grape::pc::modern {

void pushStyle(float opacity);
void popStyle();

bool beginWindow(ImTextureID logo, ImVec2 logoSize, ImVec2 logoUv);
void endWindow();
bool tab(const char* label, const char* icon, bool selected);

bool button(std::string_view label, ImVec2 requested = {});
bool checkbox(std::string_view label, bool& value);
bool dragScalar(std::string_view label, ImGuiDataType type, void* value,
                const void* minimum, const void* maximum, float speed);
bool inputText(std::string_view label, std::string_view hint,
               std::string& value);
bool beginCombo(std::string_view label, const char* preview);
bool colorButton(std::string_view label, const ImVec4& color);

bool beginSections();
void nextSection();
void endSections();
bool setSectionTitle(std::string_view title);
void hideSectionBox();

}
