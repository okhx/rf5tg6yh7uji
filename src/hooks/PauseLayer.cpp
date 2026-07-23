#include <Geode/Geode.hpp>
#include <ui/mobile_menu.hpp>
#ifndef GEODE_IS_ANDROID
#include <ui/manager.hpp>
#endif

#include "assist/hitboxes.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "checkpoint/fix.hpp"
#include "render/renderer.hpp"
#include "replay/macro.hpp"
#include "trajectory/trajectory.hpp"

#ifdef GEODE_IS_WINDOWS
#include <safetyhook.hpp>
#include "util/midhook.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/PauseLayer.hpp>

struct GrapePauseLayer : Modify<GrapePauseLayer, PauseLayer> {
    void goEdit() {
        if (Renderer::get()->isRecording()) {
            Renderer::get()->signalStop();
        }

        auto bot = GrapeEngine::get();

        bot->timeline().setPaused(false);

        bot->trajectory().uninit();
        bot->hitboxes().destroy();

        PauseLayer::goEdit();
        bot->practiceFix().removeAll();
        bot->macro().onExit();
    }

    void customSetup() override {
        PauseLayer::customSetup();

        CCSprite* sprite = CCSprite::create("grape.png"_spr);
        sprite->setScale(0.35f);

        CCMenuItemSpriteExtra* btn = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(GrapePauseLayer::onSilicateOpen));

        CCNode* menu = this->getChildByID("right-button-menu");
        menu->addChild(btn);
        menu->updateLayout();
    }

   public:
    void onSilicateOpen(CCObject*) {
#ifdef GEODE_IS_MOBILE
        MobileMenu::open();
#else
        GrapeEngine::get()->ui().m_state.m_visible->inner() = true;
#endif
    }
};

#ifdef GEODE_IS_WINDOWS
static void releaseButtonsMidhook(SafetyHookContext& ctx) {
    if (GrapeEngine::get()->isPlaying()) {
        ctx.rip += 5;
    }
}

$execute {
    util::midhook(geode::base::get() + 0x3BA239, "pauseReleaseButtons",
                  releaseButtonsMidhook);
    util::midhook(geode::base::get() + 0x3BA27C, "pauseReleaseButtons",
                  releaseButtonsMidhook);
    util::midhook(geode::base::get() + 0x3BA879, "resumeReleaseButtons",
                  releaseButtonsMidhook);
}
#endif
