#include "Geode/binding/LevelEditorLayer.hpp"
#include <Geode/binding/EditorUI.hpp>

#include <Geode/Geode.hpp>

#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "replay/system.hpp"
#include "trajectory/trajectory.hpp"
#ifdef GEODE_IS_MOBILE
#include "ui/mobile_menu.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/LevelEditorLayer.hpp>

struct SLLevelEditorLayer : Modify<SLLevelEditorLayer, LevelEditorLayer> {
    void onPlaytest() {
        auto bot = Bot::get();
        if (!bot->isEnabled()) {
            LevelEditorLayer::onPlaytest();
            return;
        }

        bot->updater().m_tpsOverflow = 0.0;
        bot->updater().m_frameOnLastAttempt = 0;
        bot->updater().setPaused(false);

        bot->updater().resetFrame();

        LevelEditorLayer::onPlaytest();

        bot->replaySystem().onReset(bot->updater().getFrame());
        bot->autoclicker().reset();
        bot->trajectory().update(this);
        bot->hitboxes().saveToTrail(this);
        m_queuedButtons.clear();

        if (bot->isPlaying()) {
            m_player1->releaseAllButtons();
            m_player2->releaseAllButtons();
            return;
        }

        bot->updater().m_canDie->inner() = false;
        bot->updater().m_inputIsDeath = false;
        bot->updater().m_expectsDeath = false;
    }

    bool init(GJGameLevel* level, bool p1) {
        if (!LevelEditorLayer::init(level, p1)) return false;

        Bot::get()->hitboxes().init(this);

        auto& t = Bot::get()->trajectory();

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

    // void destroyPlayer(PlayerObject* player, GameObject* gameObject) {
    //     auto bot = Bot::get();

    //     if (!bot->trajectory().hasDied(player)) {
    //         LevelEditorLayer::destroyPlayer(player, gameObject);
    //     }
    // }

    // void playerTookDamage(PlayerObject* player) override {
    //     auto& t = Bot::get()->trajectory();
    //     if (t.isFakePlayer(player)) {
    //         t.hasDied(player);
    //         return;
    //     }

    //     LevelEditorLayer::playerTookDamage(player);
    // }

    void update(float dt) override {
        LevelEditorLayer::update(dt);

        if (m_playbackActive) {
            Bot::get()->hitboxes().saveToTrail(this);
            Bot::get()->hitboxes().draw(this);
            Bot::get()->trajectory().update(this);
        }
    }

    void updateVisibility(float dt) override {
        LevelEditorLayer::updateVisibility(dt);

        Bot::get()->hitboxes().draw(this);
        Bot::get()->trajectory().update(this);
    }

    void updateEditor(float dt) {
        LevelEditorLayer::updateEditor(dt);

        // Editor playtests are advanced through updateEditor rather than the
        // regular GJBaseGameLayer update on every platform.
        if (m_playbackActive) {
            Bot::get()->hitboxes().saveToTrail(this);
            Bot::get()->hitboxes().draw(this);
            Bot::get()->trajectory().update(this);
        }
    }
};
