#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <functional>

class MobileMenu final : public geode::Popup {
   protected:
    enum class Page { Record, Assist, Settings };

    Page m_page = Page::Record;
    cocos2d::CCNode* m_pageNode = nullptr;
    cocos2d::CCMenu* m_pageMenu = nullptr;

    bool init();
    void rebuildPage();
    void buildRecordPage();
    void buildAssistPage();
    void buildSettingsPage();

    float columnX(int column) const;
    float rowY(int row) const;
    void addLabel(std::string const& text, float x, float y,
                  float scale = .36f,
                  cocos2d::ccColor3B color = cocos2d::ccWHITE);
    ButtonSprite* addButton(
        std::string const& text, float x, float y,
        std::function<void()> callback, float width = 82.f);
    void addToggle(std::string const& text, int row, int column, bool value,
                   std::function<void(bool)> callback);
    void addAdjuster(std::string const& text, int row, int column,
                     std::function<double()> getter,
                     std::function<void(double)> setter, double step,
                     double minimum, double maximum,
                     std::function<std::string(double)> formatter);

    void onTab(cocos2d::CCObject* sender);

   public:
    static MobileMenu* create();
    static void open();
};
