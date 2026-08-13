#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "checkpoint/fix.hpp"
#include "label/label.hpp"
#include "render/renderer.hpp"
#include "trajectory/trajectory.hpp"

using namespace geode::prelude;

#include <Geode/modify/PlayerObject.hpp>

#include "physics/collisions.hpp"
#include "physics/gjbasegamelayer.hpp"
#include "physics/object.hpp"
#include "physics/player.hpp"

struct GrapePlayerObject : Modify<GrapePlayerObject, PlayerObject> {
    void update(float dt) {
        auto bot = GrapeEngine::get();
        if (!bot->trajectory().isFakePlayer(this)) {
            bot->timeline().m_lastPlayerX = bot->timeline().m_currentPlayerX;
        }

        PlayerObject::update(dt);

        if (!bot->trajectory().isFakePlayer(this)) {
            bot->trajectory().setDelta(dt);
            bot->timeline().m_currentPlayerX = this->getPositionX();
        }
    }

    void playSpawnEffect() {
        if (GrapeEngine::get()->practiceFix().m_loadCheckpoint) {
            return;
        }

        PlayerObject::playSpawnEffect();
    }

    void playDeathEffect() {
        auto& updater = GrapeEngine::get()->timeline();
        if (updater.m_preventDeath->inner() || updater.m_predicting) {
            return;
        }

        PlayerObject::playDeathEffect();
    }

    void handleButton(bool down, int button, bool player1) {
        auto bot = GrapeEngine::get();
        if (button == 1) bot->trajectory().handleButton(player1, down);
    }

    void playSpiderDashEffect(cocos2d::CCPoint p0, cocos2d::CCPoint p1) {
        auto bot = GrapeEngine::get();
        if (!bot->trajectory().drawing()) {
            PlayerObject::playSpiderDashEffect(p0, p1);
        }
    }

#ifndef GEODE_IS_IOS
    bool levelFlipping() {
        if (LevelEditorLayer::get()) {
            return false;
        }

        return PlayerObject::levelFlipping();
    }
#endif

    void incrementJumps() {
        auto bot = GrapeEngine::get();
        if (!bot->trajectory().drawing()) {
            PlayerObject::incrementJumps();
        }
    }

    void ringJump(RingObject* ring, bool unk) {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().isFakePlayer(this)) {
            phys::ringJump(this, ring);
        } else {
            // 2.1 restore: every orb scales m_yStart, and 2.2 lowered that base
            // to ~11.03 from 2.1's 11.180032 (measured across cube/ball/robot;
            // the per-orb multipliers themselves are unchanged). Swapping the
            // base just for the duration of the call reproduces 2.1's orb
            // heights while leaving all of the game's own orb handling -- and
            // every visual effect -- untouched.
            const bool restore = GrapeSettings::get()->physics21;
            const float savedYStart = m_yStart;
            if (restore) m_yStart = 11.180032f;

            PlayerObject::ringJump(ring, unk);

            if (restore) m_yStart = savedYStart;
            bot->labels().update(Renderer::get()->isRecording());
        }
    }

    void bumpPlayer(float force, int objectType, bool playEffect,
                    GameObject* object) {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().isFakePlayer(this)) {
            phys::bumpPlayer(this, force, objectType, playEffect, object);
        } else {
            PlayerObject::bumpPlayer(force, objectType, playEffect, object);
        }
    }

    void propellPlayer(float force, bool dontPlayEffect, int objectType) {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().isFakePlayer(this)) {
            phys::propellPlayer(this, force, dontPlayEffect, objectType);
        } else {
            PlayerObject::propellPlayer(force, dontPlayEffect, objectType);
        }
    }

    void startDashing(DashRingObject* obj) {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().isFakePlayer(this)) {
            phys::startDashing(this, obj);
        } else {
            PlayerObject::startDashing(obj);
        }
    }

    void removePendingCheckpoint() {
        return;  // don't, we don't use pending checkpoints anywhere
    }

    void tryPlaceCheckpoint() {
        if (!GameManager::get()->getGameVariable("0027")) {
            return;
        }

        const double checkpointTimeout = this->m_quickCheckpointMode ? 0.2 : 1;
        if ((this->m_gameLayer->m_gameState.m_totalTime -
             this->m_lastCheckpointTime) > checkpointTimeout) {
            this->m_gameLayer->m_uiLayer->onCheck(nullptr);
            this->m_shouldTryPlacingCheckpoint = false;
            this->m_lastCheckpointTime = this->m_totalTime;
        }
    }

    void releaseAllButtons() {
        if (!GrapeEngine::get()->isEnabled()) {
            return PlayerObject::releaseAllButtons();
        }

        auto bot = GrapeEngine::get();
        auto gjbgl = GJBaseGameLayer::get();
        if ((this == gjbgl->m_player2 && !gjbgl->m_gameState.m_isDualMode) ||
            bot->timeline().m_canDie->inner() || bot->isPlaying()) {
            PlayerObject::releaseAllButtons();
        }
    }

#ifdef GEODE_IS_WINDOWS
    void stopDashing() {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().isFakePlayer(this)) {
            phys::stopDashing(this);
        } else {
            PlayerObject::stopDashing();
        }
    }
#endif
};
