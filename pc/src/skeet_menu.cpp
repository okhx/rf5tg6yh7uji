#include "skeet_menu.hpp"

#include <Geode/Geode.hpp>

#include "skrt_ui.hpp"

namespace grape::pc {
void setupSkeetFonts() {
    skrt::setup();
}

ImFont* skeetHeaderFont() {
    return skrt::headingFont();
}

int menuStyle() {
    return geode::Mod::get()->getSavedValue<int>("pc-menu-style", 0);
}

void setMenuStyle(int style) {
    geode::Mod::get()->setSavedValue("pc-menu-style", style);
}

bool useYaeMenu() { return menuStyle() == 0; }
bool useSkeetMenu() { return menuStyle() == 1; }

void pushSkeetStyle(float opacity) {
    skrt::pushStyle(opacity);
}

void popSkeetStyle() {
    skrt::popStyle();
}

bool skeetTab(const char* label, const char* icon, bool selected, float height) {
    static_cast<void>(height);
    return skrt::tab(label, icon, selected);
}
}
