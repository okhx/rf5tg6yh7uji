#ifndef FRAME_ENGINE_HPP
#define FRAME_ENGINE_HPP

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

#include <forward_list>
#include <functional>
#include <algorithm>

#include "config/config.hpp"
#include "shared/value/value.hpp"

constexpr uintptr_t TPS_STEPS_OFFSET = 0x6074e0;

class FrameEngine {
   private:
    double m_fps = 240.0;
    uint32_t m_frame = 0;

    bool _stepOnce = false;
    bool _stepBackwards = false;
    bool _paused = false;
    bool _intentionalDeath = false;
    bool _noMirror = false;
    bool _preventDeath = false;
    bool _autoFlipOnDeath = false;
    bool _predictBestPath = false;
    bool _extrapolation = false;
    bool _layoutMode = false;
    bool _noclip = false;

    std::forward_list<std::function<void(float)>> m_frozenScheduledFunctions;

   public:
    enum LockDeltaMode { Performance, Accuracy };

    int m_respawnTimer = 0;
    bool m_haltActions = false;
    bool m_onlyRefresh = false;
    void* m_actionMgr = 0;

    ConfigValuePtr<bool> m_stepOnce =
        ConfigValue<bool>::create("updater.advance_one", &_stepOnce);

    ConfigValuePtr<bool> m_stepBackwards =
        ConfigValue<bool>::create("updater.advance_back", &_stepBackwards);

    ConfigValuePtr<double> m_tps =
        ConfigValue<double>::create("updater.tps", &GrapeSettings::get()->tps);

    ConfigValuePtr<double> m_speedhack =
        ConfigValue<double>::create("updater.speedhack", &GrapeSettings::get()->speed);

    ConfigValuePtr<bool> m_paused =
        ConfigValue<bool>::create("updater.frame_advance", &_paused);

    ConfigValuePtr<bool> m_realTime = ConfigValue<bool>::create(
        "updater.real_time", &GrapeSettings::get()->realTime);
    ConfigValuePtr<uint32_t> m_maxUPR = ConfigValue<uint32_t>::create(
        "updater.max_upr", &GrapeSettings::get()->maxUpr);
    uint32_t m_stepLimit = 1;
    ConfigValuePtr<bool> m_useVisualUpdates = ConfigValue<bool>::create(
        "updater.visual_updates", &GrapeSettings::get()->useVisualUpdates);
    ConfigValuePtr<bool> m_dynamicUpr = ConfigValue<bool>::create(
        "updater.dynamic_upr", &GrapeSettings::get()->dynamicUpr);
    ConfigValuePtr<double> m_fpsTarget = ConfigValue<double>::create(
        "updater.fps_target", &GrapeSettings::get()->fpsTarget);

    ConfigValuePtr<bool> m_layoutMode =
        ConfigValue<bool>::create("updater.layout_mode", &_layoutMode);
    ConfigValuePtr<bool> m_useRegularBg = ConfigValue<bool>::create(
        "updater.layout_use_bg", &GrapeSettings::get()->useRegularBg);

    double m_tpsOverflow = 0.0;
    bool m_shouldRender = true;

    ConfigValuePtr<bool> m_lockDelta = ConfigValue<bool>::create(
        "updater.lock_delta", &GrapeSettings::get()->lockDelta);

    bool m_allowedToProcessActions = true;

    ConfigValuePtr<int> m_lockDeltaMode = ConfigValue<int>::create(
        "updater.lock_delta_mode", &GrapeSettings::get()->lockDeltaMode);

    ConfigValuePtr<bool> m_speedhackAudio = ConfigValue<bool>::create(
        "updater.speedhack_audio", &GrapeSettings::get()->speedhackAudio);

    int savedStepCount = 0;
    int totalStepCount = 0;
    int estimatedStepCount = 1;
    float currentDelta = 0.0;

    ConfigValuePtr<bool> m_canDie =
        ConfigValue<bool>::create("updater.intentional_death", &_intentionalDeath);
    ConfigValuePtr<bool> m_ssbFix = ConfigValue<bool>::create(
        "updater.ssb_fix", &GrapeSettings::get()->scrollSpeedBugFix);

    ConfigValuePtr<bool> m_backwardsStepping = ConfigValue<bool>::create(
        "updater.backwards_stepping", &GrapeSettings::get()->backwardsStepping);
    bool m_inputIsDeath = false;
    bool m_fullReset = false;
    float m_lastTfp = 0.0f;

    ConfigValuePtr<bool> m_extrapolateFrames =
        ConfigValue<bool>::create("updater.frame_extrapolation", &_extrapolation);
    cocos2d::CCPoint m_lastCameraPos;
    cocos2d::CCPoint m_currentCameraPos;

    float m_lastPlayerX;
    float m_currentPlayerX;
    float m_currentPlayerRot;

    bool m_expectsDeath = false;
    uint64_t m_frameOnLastAttempt = 0;
    uint32_t m_editorStartProgress = 0;

    ConfigValuePtr<bool> m_noMirror =
        ConfigValue<bool>::create("updater.no_mirror", &_noMirror);

    ConfigValuePtr<bool> m_preventDeath =
        ConfigValue<bool>::create("updater.prevent_death", &_preventDeath);
    ConfigValuePtr<bool> m_autoFlipOnDeath =
        ConfigValue<bool>::create("updater.auto_flip_on_death", &_autoFlipOnDeath);

    ConfigValuePtr<bool> m_predictBestPath =
        ConfigValue<bool>::create("updater.predict_best_path", &_predictBestPath);
    ConfigValuePtr<bool> m_fullGamePrediction = ConfigValue<bool>::create(
        "updater.full_game_prediction", &GrapeSettings::get()->fullGamePrediction);
    ConfigValuePtr<float> m_acceptablePrediction =
        ConfigValue<float>::create("updater.acceptable_prediction",
                               &GrapeSettings::get()->acceptablePrediction);

    enum class NoclipType { Player1, Player2, Both };

    ConfigValuePtr<bool> m_noclip =
        ConfigValue<bool>::create("updater.noclip", &_noclip);
    NoclipType m_noclipType = static_cast<NoclipType>(
        std::clamp(GrapeSettings::get()->noclipPlayer, 0, 2));

    bool m_isAutoFlipped = false;
    bool m_predicting = false;

    cocos2d::CCLabelBMFont* m_frameLabel = nullptr;

    float getTimeWarp();

    void calculateSteps(float dt, float targetDt);
    void breakLoop();
    void runUpdates(std::function<void(float)> update, float realDt,
                    bool frozen);
    void runFrozenTick();

    void scheduleFrozenFunction(std::function<void(float)> func) {
        m_frozenScheduledFunctions.push_front(func);
    }

    void setFps(double fps);
    void setSpeed(double speed);
    void setTps(double tps) {
        if (tps <= 0.0) return;




        m_tps->inner() = tps;
    }
    void setPaused(bool paused) { m_paused->inner() = paused; }
    void togglePaused() { m_paused->inner() = !m_paused->inner(); }
    void stepOnce() { m_stepOnce->inner() = true; }
    bool consumeStep() {
        bool shouldStep = m_stepOnce->inner();
        m_stepOnce->inner() = false;
        return shouldStep;
    }

    void backwardsStep(int n = 1);

    void updateAudioSpeedhack();

    inline double getTps() const { return m_tps->inner(); }
    inline double getDt() { return 1. / m_fps; }
    inline double getVisualDt();
    inline double getPhysicsDt() { return 1. / m_tps->inner(); };

    [[nodiscard]] uint32_t getFrame();
    [[nodiscard]] uint32_t getDisplayFrame();

    /**
     * ONLY USE IF YOU KNOW WHAT YOU ARE DOING.
     * Incrementing the frame WILL forward time in the macro.
     */
    void incrementFrame(uint32_t count = 1) { m_frame += count; }
    void resetFrame() { m_frame = 0; }
    void setFrame(uint32_t frame) { m_frame = frame; }

    bool isPaused() { return m_paused->inner(); }
    bool isLockDelta();
    bool useFastLockDelta();

    void findBestFrameCandidate();
    void portableFrameUpdate(PlayLayer* playLayer, float visualDt);
};

#endif
