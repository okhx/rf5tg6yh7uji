#include "Geode/binding/GJBaseGameLayer.hpp"

#ifdef GEODE_IS_WINDOWS
#include <Zydis/Zydis.h>
#endif

#include <Geode/Geode.hpp>
#include <Geode/binding/AdvancedFollowInstance.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#ifdef GEODE_IS_WINDOWS
#include <safetyhook.hpp>
#endif
#include <vector>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "checkpoint/fix.hpp"
#include "physics/collisions.hpp"
#include "physics/gjbasegamelayer.hpp"
#include "replay/macro.hpp"
#include "trajectory/trajectory.hpp"
#ifdef GEODE_IS_WINDOWS
#include "util/midhook.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/GJBaseGameLayer.hpp>

#ifdef GEODE_IS_WINDOWS
$execute {
    geode::log::info("Patching {} (GJBaseGameLayer::resetLevelVariables)",
                     base::get() + 0x23B4EA);
    if (!Mod::get()->patch(reinterpret_cast<void*>(base::get() + 0x23B4EA),
                           {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90})) {
        geode::log::error(
            "Failed to patch GJBaseGameLayer::resetLevelVariables");
    }

    if (!Mod::get()->patch(
            reinterpret_cast<void*>(base::get() + 0x4cd95c),
            {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
             0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90})) {
        geode::log::error("Failed to patch UILayer::handleKeypress");
    }

    if (!Mod::get()->patch(reinterpret_cast<void*>(base::get() + 0x3B994C),
                           {0xeb, 0x5e})) {
        geode::log::error("Failed to patch PlayLayer::resetLevel");
    }

    if (!Mod::get()->patch(reinterpret_cast<void*>(base::get() + 0x3BA508),
                           {0x90, 0x90, 0x90, 0x90, 0x90})) {
        geode::log::error("Failed to patch PlayLayer::resumeAndRestart");
    }
}
#endif

struct GrapeGJBaseGameLayer : Modify<GrapeGJBaseGameLayer, GJBaseGameLayer> {
    struct PlayerState {
        float m_rotation;

        void saveFromPlayer(PlayerObject* player) {
            m_rotation = player->getRotation();
        }

        void loadIntoPlayer(PlayerObject* player) {
            player->setRotation(m_rotation);
        }
    };

    struct GroundState {
        CCPoint m_sprite1Pos;
        CCPoint m_sprite2Pos;

        void saveFromGround(GJGroundLayer* layer) {
            if (!layer) return;
            if (!layer->m_ground1Sprite) return;

            m_sprite1Pos = layer->m_ground1Sprite->getPosition();
            if (layer->m_ground2Sprite) {
                m_sprite2Pos = layer->m_ground2Sprite->getPosition();
            }
        }

        void loadIntoGround(GJGroundLayer* layer) {
            if (!layer) return;
            if (!layer->m_ground1Sprite) return;

            layer->m_ground1Sprite->setPosition(m_sprite1Pos);
            if (layer->m_ground2Sprite) {
                layer->m_ground2Sprite->setPosition(m_sprite2Pos);
            }
        }
    };

    struct Fields {
        GJGameState m_lastActualGameState;
        GroundState m_lastGround;
        GroundState m_lastGround2;

        PlayerState m_lastActualP1;
        PlayerState m_lastActualP2;
    };

    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("GJBaseGameLayer::handleButton",
                                     Priority::Last)) {
            geode::log::warn(
                "Failed to set priority for handleButton! Alt hook may not "
                "work as expected");
        }
    }

    bool shouldExtrapolate() {
        auto& updater = GrapeEngine::get()->timeline();
        if (updater.estimatedStepCount != 0) {
            return false;
        }

        return true;
    }

    void extrapolateVisualUpdates(float dt) {
        auto& updater = GrapeEngine::get()->timeline();

        float dt60 = dt * 60.0;

        using Mode = GrapeSettings::TrajectorySettings::Mode;
        auto* t = GrapeEngine::get()->trajectory().unsafeInner();

        m_player1->setRotation(m_fields->m_lastActualP1.m_rotation);
        m_player2->setRotation(m_fields->m_lastActualP2.m_rotation);

        Mode m = m_player1->m_jumpBuffered ? Mode::Hold : Mode::Release;
        auto prediction = t->simulate(this, true, Mode::Player1 | m, true,
                                      {
                                          .m_bypassConfig = true,
                                          .m_maxLength = 0,
                                      });

        Mode m2 = m_player2->m_jumpBuffered ? Mode::Hold : Mode::Release;
        auto prediction2 = t->simulate(this, true, Mode::Player2 | m2, true,
                                       {
                                           .m_bypassConfig = true,
                                           .m_maxLength = 0,
                                       });

        float framePosition = updater.m_tpsOverflow / updater.getPhysicsDt();

        CCPoint lerped = prediction.position * framePosition +
                         m_player1->m_position * (1 - framePosition);
        float lerpedRot =
            prediction.rotation * framePosition +
            m_fields->m_lastActualP1.m_rotation * (1 - framePosition);

        CCPoint lerped2 = prediction2.position * framePosition +
                          m_player2->m_position * (1 - framePosition);
        float lerpedRot2 =
            prediction2.rotation * framePosition +
            m_fields->m_lastActualP2.m_rotation * (1 - framePosition);

        this->m_player1->setPosition(lerped);
        this->m_player2->setPosition(lerped2);
        this->m_player1->setRotation(lerpedRot);
        this->m_player2->setRotation(lerpedRot2);

        this->updateCamera(dt60);
        this->updateVisibility(dt);

        this->CCLayer::update(dt);
    }

    void storeActualState() {
        m_fields->m_lastActualP1.saveFromPlayer(m_player1);
        m_fields->m_lastActualP2.saveFromPlayer(m_player2);
        m_fields->m_lastActualGameState = m_gameState;
        m_fields->m_lastGround.saveFromGround(m_groundLayer);
        m_fields->m_lastGround2.saveFromGround(m_groundLayer2);
    }

    void loadActualState() {
        m_gameState = m_fields->m_lastActualGameState;
        m_fields->m_lastActualP1.loadIntoPlayer(m_player1);
        m_fields->m_lastActualP2.loadIntoPlayer(m_player2);
        m_fields->m_lastGround.loadIntoGround(m_groundLayer);
        m_fields->m_lastGround2.loadIntoGround(m_groundLayer2);
    }

    void update(float dt) override {
        auto& updater = GrapeEngine::get()->timeline();
        if (updater.m_onlyRefresh || !GrapeEngine::get()->isEnabled()) {
            GJBaseGameLayer::update(dt);
            return;
        }

        if (!updater.isLockDelta() || !PlayLayer::get()) {
            updater.calculateSteps(dt, updater.getPhysicsDt());
        }

        if (updater.m_respawnTimer > 0) {
            updater.m_respawnTimer--;
            updater.totalStepCount = std::min(updater.totalStepCount, 1);
            updater.estimatedStepCount =
                std::min(updater.estimatedStepCount, 1);
        }

        if (updater.m_extrapolateFrames->inner() &&
            updater.getFrame() > updater.m_frameOnLastAttempt) {
            if (this->shouldExtrapolate()) {
                this->extrapolateVisualUpdates(dt);
            } else {
                this->loadActualState();
                GJBaseGameLayer::update(dt);
                this->storeActualState();
            }
        } else {
            if (updater.isLockDelta() || updater.estimatedStepCount != 0) {
                GJBaseGameLayer::update(dt);
                this->storeActualState();
            }
        }
    }

    void gameEventTriggered(GJGameEvent event, int p1, int p2) {
        auto bot = GrapeEngine::get();

        if (bot->trajectory().drawing()) return;
        if (event == GJGameEvent::CheckpointRespawn &&
            !GrapeEngine::get()->practiceFix().m_shouldLoadPlatformer)
            return;

        GJBaseGameLayer::gameEventTriggered(event, p1, p2);
    }

    void destroyObject(GameObject* obj) {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().drawing()) return;

        if (this->m_isPracticeMode) {
            bot->practiceFix().registerBrokenObject(obj);
        }

        GJBaseGameLayer::destroyObject(obj);
    }

    void updateCamera(float dt) {
        GrapeEngine::get()->timeline().m_lastCameraPos =
            GrapeEngine::get()->timeline().m_currentCameraPos;
        GJBaseGameLayer::updateCamera(dt);
        GrapeEngine::get()->timeline().m_currentCameraPos = m_objectLayer->getPosition();
    }

    double getModifiedDelta(float dt) {
        if (!GrapeEngine::get()->isEnabled()) {
            return GJBaseGameLayer::getModifiedDelta(dt);
        }

        float modDelta = GJBaseGameLayer::getModifiedDelta(dt);
        if (LevelEditorLayer::get()) {
            return modDelta;
        }

        auto& updater = GrapeEngine::get()->timeline();
        if (updater.m_onlyRefresh) {
            return 0.0f;
        }

        float wantedDt =
            updater.getPhysicsDt() * fminf(m_gameState.m_timeWarp, 1.0f);

        return wantedDt;
    }

    void addInputToReplay(PlayerButtonCommand cmd) {
        if (cmd.m_isPlayer2 && !this->m_levelSettings->m_twoPlayerMode) {
            cmd.m_isPlayer2 = false;
        }

        auto bot = GrapeEngine::get();
        auto& replay = bot->macro().m_actionAtom;
        if (replay.length() > 0 &&
            replay.m_actions.back() > bot->timeline().getFrame()) {
            return;
        }

        auto _result = replay.addAction(
            bot->timeline().getFrame(),
            static_cast<slc::Action::ActionType>(cmd.m_button), cmd.m_isPush,
            bot->macro().playerFlipped(cmd.m_isPlayer2));
    }

    void performMaintainGravity() {
        auto bot = GrapeEngine::get();

        m_queuedButtons.clear();

        auto p1 = this->m_player1;
        auto p2 = this->m_player2;
        if (PlayLayer::get()->m_levelEndAnimationStarted) {
            return;
        }

        bool p1Jumping = m_uiLayer->m_p1Jumping || m_uiLayer->m_p1TouchId != -1;
        bool p2Jumping = m_uiLayer->m_p2Jumping || m_uiLayer->m_p2TouchId != -1;
        if (bot->macro().hasFlippedControls()) {
            std::swap(p1Jumping, p2Jumping);
        }

        if (bot->macro().m_mirrorInputs) {
            p1Jumping |= p2Jumping ^ bot->macro().m_mirrorInverted;
            p2Jumping |= p1Jumping ^ bot->macro().m_mirrorInverted;
        }

        while (p1->m_isUpsideDown == (p1Jumping == p1->m_jumpBuffered) &&
               !p1->m_controlsDisabled) {
            addInputToReplay(PlayerButtonCommand{
                .m_button = static_cast<PlayerButton>(1),
                .m_isPush = (bool)(p1Jumping ^ p1->m_isUpsideDown),
                .m_isPlayer2 = bot->macro().playerFlipped(false),
                .m_step = 0,
                .m_timestamp = 0.0});

            this->handleButton(p1Jumping ^ p1->m_isUpsideDown, 1,
                               bot->macro().playerFlipped(true));
        }

        while (this->m_gameState.m_isDualMode &&
               this->m_levelSettings->m_twoPlayerMode &&
               p2->m_isUpsideDown == (p2Jumping == p2->m_jumpBuffered) &&
               !p2->m_controlsDisabled) {
            addInputToReplay(PlayerButtonCommand{
                .m_button = static_cast<PlayerButton>(1),
                .m_isPush = (bool)(p2Jumping ^ p2->m_isUpsideDown),
                .m_isPlayer2 = bot->macro().playerFlipped(true),
                .m_step = 0,
                .m_timestamp = 0.0});

            this->handleButton(p2Jumping ^ p2->m_isUpsideDown, 1,
                               bot->macro().playerFlipped(false));
        }
    }

    void requeueInverted() {
        auto bot = GrapeEngine::get();

        for (auto& cmd : m_queuedButtons) {
            bot->macro().m_lastInputs.insert_or_assign(
                static_cast<int>(cmd.m_button) +
                    ((cmd.m_isPlayer2 && this->m_levelSettings->m_twoPlayerMode)
                         ? 4
                         : 0),
                slc::Action(
                    bot->macro().m_forceNextInput
                        ? std::numeric_limits<uint32_t>::max()
                        : bot->timeline().getFrame(),
                    0, static_cast<slc::Action::ActionType>(cmd.m_button),
                    cmd.m_isPush,
                    cmd.m_isPlayer2 && this->m_levelSettings->m_twoPlayerMode));

            if (!bot->macro().m_mirrorInputs) continue;

            bool inverted = bot->macro().m_mirrorInverted;

            this->queueButton(
                static_cast<int>(cmd.m_button), cmd.m_isPush ^ inverted,
                bot->macro().playerFlipped(!cmd.m_isPlayer2), 0.0);
        }
    }

    void handleButton(bool pressed, int button, bool player1) {
        auto bot = GrapeEngine::get();
        if (!bot->macro().m_useAlternateHook->inner() ||
            !bot->isRecording()) {
            return GJBaseGameLayer::handleButton(pressed, button, player1);
        }

        addInputToReplay({
            .m_button = static_cast<PlayerButton>(button),
            .m_isPush = pressed,
            .m_isPlayer2 = !player1,
            .m_step = 0,
            .m_timestamp = 0.0,
        });
        return GJBaseGameLayer::handleButton(pressed, button, player1);
    }

    void saveQueuedButtons() {
        for (auto& cmd : m_queuedButtons) {
            addInputToReplay(cmd);
        }
    }

    bool processReplayAction(slc::Action& action) {
        auto bot = GrapeEngine::get();

        int button = static_cast<int>(action.m_type);
        if (action.m_type == slc::Action::ActionType::Death) {
            bot->timeline().m_expectsDeath = true;
            bot->macro().m_startingSeedThisAttempt =
                bot->macro().getCurrentRandomState();
            return false;
        }

        if (action.m_type == slc::Action::ActionType::RestartFull) {
            bot->timeline().m_expectsDeath = true;
            bot->macro().m_startingSeedThisAttempt =
                bot->macro().getCurrentRandomState();
            ((PlayLayer*)this)->fullReset();
            return true;
        }

        if (action.m_type == slc::Action::ActionType::Restart) {
            bot->timeline().m_expectsDeath = true;
            bot->macro().m_startingSeedThisAttempt =
                bot->macro().getCurrentRandomState();
            ((PlayLayer*)this)->resetLevel();
            return true;
        }

        if (action.m_type == slc::Action::ActionType::TPS) {
            bot->timeline().m_tps->inner() = action.m_tps;
            bot->timeline().estimatedStepCount = 0;
            bot->timeline().totalStepCount = 0;
            return false;
        }

        if (button == 0 || button > 3) {
            return false;
        }

        this->queueButton(button, action.m_holding,
                          bot->macro().playerFlipped(action.m_player2),
                          0);
        return false;
    }

    void performAutoFlipOnDeath() {
        auto bot = GrapeEngine::get();

        if (PlayLayer::get()->m_levelEndAnimationStarted) {
            return;
        }

        bool p1Jumping = m_uiLayer->m_p1Jumping || m_uiLayer->m_p1TouchId != -1;
        bool p2Jumping = m_uiLayer->m_p2Jumping || m_uiLayer->m_p2TouchId != -1;
        if (bot->macro().hasFlippedControls()) {
            std::swap(p1Jumping, p2Jumping);
        }

        if (bot->macro().m_mirrorInputs) {
            p1Jumping |= p2Jumping ^ bot->macro().m_mirrorInverted;
            p2Jumping |= p1Jumping ^ bot->macro().m_mirrorInverted;
        }

        if (this->m_player1->m_jumpBuffered != bot->timeline().m_isAutoFlipped) {
            m_queuedButtons.push_back(
                {.m_button = (PlayerButton)1,
                 .m_isPush = bot->timeline().m_isAutoFlipped,
                 .m_isPlayer2 = false,
                 .m_step = 0,
                 .m_timestamp = 0.0});
        }
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        auto bot = GrapeEngine::get();
        if (!bot->isEnabled()) {
            return GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
        }

        bot->practiceFix().updatePlatformerInputs(m_queuedButtons);

        if (bot->macro().m_ignoreInputs->inner() && bot->isPlaying()) {
            m_queuedButtons.clear();
        }

        if (bot->isRecording()) {
            if (bot->macro().m_maintainGravity) {
                this->performMaintainGravity();
                return;
            }

            this->requeueInverted();
            if (!bot->macro().m_useAlternateHook->inner()) {
                this->saveQueuedButtons();
            }
        } else if (!LevelEditorLayer::get() || bot->isPlaying()) {
            uint32_t frame = bot->timeline().getFrame();

            if (auto input = bot->macro().getCurrentQueuedInput();
                input.has_value()) {
                uint32_t delayInFrames = bot->timeline().m_tps->inner() * 0.5;

                if (input.value().m_type ==
                        slc::Action::ActionType::RestartFull &&
                    input.value().m_frame == frame + delayInFrames) {
                    if (auto ell = this->getChildByID("EndLevelLayer"); ell) {
                        FMODAudioEngine::sharedEngine()->clearAllAudio();
                        FMODAudioEngine::sharedEngine()->playEffect(
                            "playSound_01.ogg", 1.0, 0.0, 0.3);
                        ((GJDropDownLayer*)ell)->exitLayer(nullptr);
                    }
                }
            }

            while (auto input = bot->macro().getNextInput(frame)) {
                if (input.has_value() &&
                    this->processReplayAction(input.value())) {
                    return;
                }
            }
        }

        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);

        if (bot->isRecording()) {
            if (bot->timeline().m_autoFlipOnDeath->inner()) {
                this->performAutoFlipOnDeath();
                this->saveQueuedButtons();
                GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
                return;
            }
        }
    }

    void collisionCheckObjects(PlayerObject* player,
                               gd::vector<GameObject*>* objects, int length,
                               float p3) {
        auto bot = GrapeEngine::get();

        if (!bot->trajectory().isFakePlayer(player)) {
            return GJBaseGameLayer::collisionCheckObjects(player, objects,
                                                          length, p3);
        }

        phys::collisionCheckObjects(this, player, objects, length, p3);
    }

    void teleportPlayer(TeleportPortalObject* obj, PlayerObject* player) {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().isFakePlayer(player)) {
            phys::teleportPlayer(this, obj, player);
        } else {
            GJBaseGameLayer::teleportPlayer(obj, player);
        }
    }

    void flipGravity(PlayerObject* player, bool gravity, bool unk) {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().isFakePlayer(player)) {
            phys::flipGravity(player, gravity);
        } else {
            GJBaseGameLayer::flipGravity(player, gravity, unk);
        }
    }

    void toggleFlipped(bool flipped, bool noEffects) {
        if (GrapeEngine::get()->timeline().m_noMirror->inner()) {
            this->m_gameState.m_unkBool10 = flipped;
            return;
        }

        GJBaseGameLayer::toggleFlipped(flipped, noEffects);
    }

    void processMoveActionsStep(float dt, bool visibleFrame) {
        const auto& randomHashObject = [](GameObject* object, int group,
                                          int index) {
            return (GrapeEngine::get()->macro().m_startingSeed *
                    (int)(object->m_objectID + 5541) *
                    (int)(fabs(object->m_startPosition.x) + 1.0) *
                    (int)(fabs(object->m_startPosition.y) + 1.0) *
                    ((int)object->m_zLayer + 2137) *
                    (object->m_groupCount + 6969) *
                    ((int)object->m_isObjectBlack * 420 + 14) *
                    ((int)(fabs(object->m_startRotationX) * 6.7 + 1.0) *
                     (index + 67)) *
                    (group + 2137)) %
                   1777;
        };

        const auto& updateAllObjectsForGroup = [&, this](int group) {
            cocos2d::CCArray* objects = this->getGroup(group);
            for (int i = 0; i < (int)objects->count(); i++) {
                GameObject* object = (GameObject*)objects->objectAtIndex(i);
                object->m_varianceIndex = randomHashObject(object, group, i);
            }
        };

        for (auto& a : m_gameState.m_advanceFollowInstances) {
            updateAllObjectsForGroup(a.m_group);
        }

        for (auto& a : m_gameState.m_rotateEffectInstances) {
            updateAllObjectsForGroup(a.m_targetID);
        }

        for (auto& a : m_gameState.m_scaleEffectInstances) {
            updateAllObjectsForGroup(a.m_targetID);
        }

        for (auto& a : m_gameState.m_moveEffectInstances) {
            updateAllObjectsForGroup(a.m_targetID);
        }

        GJBaseGameLayer::processMoveActionsStep(dt, visibleFrame);
    }

    void createBackground(int background) {
        auto bot = GrapeEngine::get();
        if (!bot->isEnabled() || LevelEditorLayer::get()) {
            GJBaseGameLayer::createBackground(background);
            return;
        }

        auto& u = bot->timeline();
        if (u.m_layoutMode->inner() && !u.m_useRegularBg->inner()) {
            background = 13;
        }

        GJBaseGameLayer::createBackground(background);
    }

    void createMiddleground(int mg) {
        auto bot = GrapeEngine::get();
        if (!bot->isEnabled() || !bot->timeline().m_layoutMode->inner() ||
            !bot->timeline().m_useRegularBg->inner() ||
            LevelEditorLayer::get()) {
            GJBaseGameLayer::createMiddleground(mg);
        } else {
            if (m_middleground) {
                m_middleground->removeFromParent();
                m_middleground = nullptr;
            }
        }
    }

    void createGroundLayer(int ground, int lineType) {
        auto bot = GrapeEngine::get();
        if (!bot->isEnabled() || LevelEditorLayer::get() ||
            !bot->timeline().m_useRegularBg->inner()) {
            GJBaseGameLayer::createGroundLayer(ground, lineType);
            return;
        }

        if (bot->timeline().m_layoutMode->inner()) {
            ground = 18;
            lineType = 2;
        }

        GJBaseGameLayer::createGroundLayer(ground, lineType);
    }

    void updateColor(cocos2d::ccColor3B& color, float fadeTime, int colorID,
                     bool blending, float opacity, cocos2d::ccHSVValue& copyHSV,
                     int colorIDToCopy, bool copyOpacity,
                     EffectGameObject* callerObject, int unk1,
                     int unk2) override {
        if (!LevelEditorLayer::get() && GrapeEngine::get()->isEnabled() &&
            GrapeEngine::get()->timeline().m_layoutMode->inner()) {
            return;
        }

        GJBaseGameLayer::updateColor(color, fadeTime, colorID, blending,
                                     opacity, copyHSV, colorIDToCopy,
                                     copyOpacity, callerObject, unk1, unk2);
    }

    void triggerGradientCommand(GradientTriggerObject* obj) {
        if (!LevelEditorLayer::get() && GrapeEngine::get()->isEnabled() &&
            GrapeEngine::get()->timeline().m_layoutMode->inner()) {
            return;
        }

        GJBaseGameLayer::triggerGradientCommand(obj);
    }
};

#ifdef GEODE_IS_WINDOWS
static void shakeRandomOverride(SafetyHookContext& ctx) {
    uint64_t& state = GrapeEngine::get()->macro().m_shakeRandomState;
    state = (int)((214013 * state + 2531011) >> 16) & 0x7FFF;

    ctx.rax = (uintptr_t)state;
}

static void overrideCheckpointPlacement(SafetyHookContext& ctx) {
    ctx.rip += 5;
    PlayLayer::get()->queueCheckpoint();
}

$execute {
    util::midhook(geode::base::get() + 0x23E173, "shakeRandomOverride",
                  shakeRandomOverride);
    util::midhook(geode::base::get() + 0x23E1A1, "shakeRandomOverride",
                  shakeRandomOverride);
    util::midhook(geode::base::get() + 0x23E1CB, "shakeRandomOverride",
                  shakeRandomOverride);
    util::midhook(geode::base::get() + 0x23E1E9, "shakeRandomOverride",
                  shakeRandomOverride);
    util::midhook(geode::base::get() + 0x3A3657, "checkpointPlacement",
                  overrideCheckpointPlacement);
}
#endif

$execute {
    GrapeEngine::get()->timeline().m_lockDelta->handle(
        [](bool&) { GrapeEngine::get()->timeline().m_tpsOverflow = 0.0; });
}
