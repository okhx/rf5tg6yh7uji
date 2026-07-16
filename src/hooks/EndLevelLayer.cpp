#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

#include "settings/settings.hpp"
#include "ui/mobile_menu.hpp"

using namespace geode::prelude;

#ifdef GEODE_IS_MOBILE
struct SLEndLevelLayer : Modify<SLEndLevelLayer, EndLevelLayer> {
    void customSetup() override {
        EndLevelLayer::customSetup();
        if (!SLSettings::get()->showEndMenuButton) return;

        auto* sprite = CCSprite::create("grape.png"_spr);
        sprite->setScale(.32f);
        auto* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(SLEndLevelLayer::onGrape));
        auto* menu = CCMenu::create();
        menu->addChild(button);
        auto size = CCDirector::get()->getWinSize();
        menu->setPosition({size.width - 38.f, 38.f});
        this->addChild(menu, 1000);
    }

    void onGrape(CCObject*) { MobileMenu::open(); }
};
#endif
