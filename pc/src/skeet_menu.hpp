#pragma once

struct ImFont;

namespace grape::pc {
void setupSkeetFonts();
ImFont* skeetHeaderFont();
int menuStyle();
void setMenuStyle(int style);
bool useYaeMenu();
bool useSkeetMenu();
void pushSkeetStyle(float opacity);
void popSkeetStyle();
bool skeetTab(const char* label, const char* icon, bool selected, float height);
}
