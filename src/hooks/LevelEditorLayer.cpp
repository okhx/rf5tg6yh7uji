#include "Geode/binding/LevelEditorLayer.hpp"
#include <Geode/binding/EditorUI.hpp>

#include <Geode/Geode.hpp>

#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "replay/macro.hpp"
#include "render/renderer.hpp"
#include "trajectory/trajectory.hpp"
#ifdef GEODE_IS_MOBILE
#include "ui/mobile_menu.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/LevelEditorLayer.hpp>

struct GrapeLevelEditorLayer : Modify<GrapeLevelEditorLayer, LevelEditorLayer> {
    void onPlaytest() {
        auto bot = GrapeEngine::get();
        if (!bot->isEnabled()) {
            LevelEditorLayer::onPlaytest();
            return;
        }

        bot->timeline().m_tpsOverflow = 0.0;
        bot->timeline().m_frameOnLastAttempt = 0;
        bot->timeline().setPaused(false);

        bot->timeline().resetFrame();

        LevelEditorLayer::onPlaytest();

        bot->timeline().m_editorStartProgress = static_cast<uint32_t>(
            std::max(0, static_cast<int>(m_gameState.m_currentProgress)));
        bot->macro().onReset(0);
        if (bot->isPlaying()) {
            bot->macro().getCurrentRandomState() =
                bot->macro().m_startingSeed;
            bot->macro().m_startingSeedThisAttempt =
                bot->macro().m_startingSeed;
        }
        bot->autoclicker().reset();
        bot->trajectory().update(this);
        bot->hitboxes().saveToTrail(this);
        m_queuedButtons.clear();

        if (bot->isPlaying()) {
            m_player1->releaseAllButtons();
            m_player2->releaseAllButtons();
            return;
        }

        bot->timeline().m_canDie->inner() = false;
        bot->timeline().m_inputIsDeath = false;
        bot->timeline().m_expectsDeath = false;
    }

    bool init(GJGameLevel* level, bool p1) {
        if (!LevelEditorLayer::init(level, p1)) return false;

        GrapeEngine::get()->hitboxes().init(this);

        auto& t = GrapeEngine::get()->trajectory();

        if (t.exists()) {
            t.uninit();
        }

        t.init();

#ifdef GEODE_IS_MOBILE
        if (m_editorUI) {
            auto* menu = CCMenu::create();
            menu->setPosition({0.f, 0.f});
            menu->setID("grape-editor-menu");
            auto* sprite = CCSprite::create("grape.png"_spr);
            if (!sprite) return true;
            sprite->setScale(.31f);
            auto* button = geode::cocos::CCMenuItemExt::createSpriteExtra(
                sprite, [](CCMenuItemSpriteExtra*) { MobileMenu::open(); });
            const auto winSize = CCDirector::get()->getWinSize();
            button->setPosition({winSize.width - 112.f,
                                 winSize.height - 20.f});
            menu->addChild(button);
            m_editorUI->addChild(menu, 1000);
        }
#endif

        return true;
    }





    void update(float dt) override {
        LevelEditorLayer::update(dt);

        if (m_playbackActive) {
            GrapeEngine::get()->hitboxes().saveToTrail(this);
            GrapeEngine::get()->hitboxes().draw(this);
            GrapeEngine::get()->trajectory().update(this);
        }
    }

    void updateVisibility(float dt) override {
        LevelEditorLayer::updateVisibility(dt);

        GrapeEngine::get()->hitboxes().draw(this);
        GrapeEngine::get()->trajectory().update(this);
    }

    void updateEditor(float dt) {
        auto* bot = GrapeEngine::get();
        if (m_playbackActive && bot->timeline().isPaused() &&
            !Renderer::get()->isRecording() &&
            !bot->timeline().consumeStep()) {
            bot->hitboxes().draw(this);
            bot->trajectory().update(this);
            return;
        }

        LevelEditorLayer::updateEditor(dt);

        if (m_playbackActive) {
            bot->hitboxes().saveToTrail(this);
            bot->hitboxes().draw(this);
            bot->trajectory().update(this);
        }
    }
};
