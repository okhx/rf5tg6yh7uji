#include "Geode/binding/LevelEditorLayer.hpp"

#include <Geode/Geode.hpp>

#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "replay/system.hpp"
#include "trajectory/trajectory.hpp"

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
        bot->hitboxes().clearTrail();

        m_queuedButtons.clear();

        if (bot->isPlaying()) {
            m_player1->releaseAllButtons();
            m_player2->releaseAllButtons();

            this->processQueuedButtons(0.0, true);

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

    void updateVisibility(float dt) override {
        LevelEditorLayer::updateVisibility(dt);

        Bot::get()->hitboxes().draw(this);
        Bot::get()->trajectory().update(this);
    }

    void updateEditor(float dt) { LevelEditorLayer::updateEditor(dt); }
};
