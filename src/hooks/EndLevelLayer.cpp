#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

#include "assist/hitboxes.hpp"
#include "checkpoint/fix.hpp"
#include "config/config.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "replay/macro.hpp"
#include "render/renderer.hpp"
#include "trajectory/trajectory.hpp"
#include "ui/mobile_menu.hpp"

using namespace geode::prelude;

#ifdef GEODE_IS_MOBILE
struct GrapeEndLevelLayer : Modify<GrapeEndLevelLayer, EndLevelLayer> {
#ifdef GEODE_IS_IOS
    void onEdit(CCObject* sender) {
        GrapeEngine::get()->timeline().setPaused(false);
        EndLevelLayer::onEdit(sender);
    }

    void goEdit() {
        auto* bot = GrapeEngine::get();
        bot->timeline().setPaused(false);
        bot->trajectory().uninit();
        bot->hitboxes().destroy();

        EndLevelLayer::goEdit();
        bot->practiceFix().removeAll();
        bot->macro().onExit();
    }

    void enterAnimFinished() override {
        EndLevelLayer::enterAnimFinished();
        Renderer::get()->notifyEndLevelMenuReady();
    }
#endif

    void customSetup() override {
        EndLevelLayer::customSetup();
        if (!GrapeSettings::get()->showEndMenuButton) return;

        auto* sprite = CCSprite::create("grape.png"_spr);
        sprite->setScale(.32f);
        auto* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(GrapeEndLevelLayer::onGrape));
        auto* menu = CCMenu::create();
        menu->addChild(button);
        auto size = CCDirector::get()->getWinSize();
        menu->setPosition({size.width - 38.f, 38.f});
        this->addChild(menu, 1000);
    }

    void onGrape(CCObject*) { MobileMenu::open(); }
};
#endif
