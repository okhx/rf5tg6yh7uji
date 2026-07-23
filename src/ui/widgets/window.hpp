#include <string>
#include <vector>

#include "imgui.h"
#include "tab.hpp"

class MenuWindow {
    std::string m_name;
    bool m_visible = false;

    std::vector<MenuTab> m_tabs;
    size_t m_currentTab = 0;

    MenuWindow(std::string name) : m_name(name) {}
};