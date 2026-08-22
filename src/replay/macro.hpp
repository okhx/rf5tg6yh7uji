#ifndef MACRO_ENGINE_HPP
#define MACRO_ENGINE_HPP

#include <Geode/utils/file.hpp>
#include <slc/slc.hpp>
#include <unordered_map>

#include "../config/config.hpp"
#include "../shared/value/value.hpp"
#include "engine/engine.hpp"
#include "engine/clock.hpp"

struct ReplayMeta {
    uint64_t seed;
    char reserved[56];
};

class MacroEngine {
   private:
    bool processSlc3(slc::v3::Replay<>& replay);
    bool processSlc2(slc::v2::Replay<ReplayMeta>& replay);

   public:
    size_t m_inputIndex = 0;

    slc::ActionAtom m_actionAtom;
    bool m_collectInputs = true;
    ConfigValuePtr<bool> m_ignoreInputs = ConfigValue<bool>::create(
        "replay.ignore_inputs", &GrapeSettings::get()->blockInputs);
    ConfigValuePtr<bool> m_autosaveAtLevelEnd = ConfigValue<bool>::create(
        "replay.autosave_at_level_end", &GrapeSettings::get()->autosaveAtLevelEnd);

    ConfigValuePtr<bool> m_useAlternateHook = ConfigValue<bool>::create(
        "replay.althook", &GrapeSettings::get()->useAlternateHook);

    bool m_overrideSeed = false;
    uint64_t m_overriddenSeed = 0;
    uint64_t m_startingSeed = 0;
    uint64_t m_startingSeedThisAttempt = 0;
    uint64_t m_shakeRandomState = 0;
    uint64_t m_teleportRandomState = 0;
#ifndef GEODE_IS_WINDOWS
    uint64_t m_portableRandomState = 0;
#endif
    bool m_flipProcessingInputs = false;

    bool m_forceNextInput = false;
    std::unordered_map<int, slc::Action> m_lastInputs;

    bool m_mirrorInputs = false;
    bool m_mirrorInverted = false;
    bool m_maintainGravity = false;
    ConfigValuePtr<bool> m_mirrorInputsValue = ConfigValue<bool>::create(
        "replay.mirror_inputs", &m_mirrorInputs);
    ConfigValuePtr<bool> m_mirrorInvertedValue = ConfigValue<bool>::create(
        "replay.mirror_inverted", &m_mirrorInverted);
    ConfigValuePtr<bool> m_maintainGravityValue = ConfigValue<bool>::create(
        "replay.maintain_gravity", &m_maintainGravity);

    std::string m_replayName = "";
    bool m_lastOperationSucceeded = false;

    GameScheduler::JobId m_autosaveId;
    ConfigValuePtr<bool> m_autosaveAtInterval = ConfigValue<bool>::create(
        "replay.autosave_at_interval", &GrapeSettings::get()->autosaveAtInterval);
    ConfigValuePtr<double> m_autosaveInterval = ConfigValue<double>::create(
        "replay.autosave_interval", &GrapeSettings::get()->autosaveInterval);

    size_t getInputIndex() const { return m_inputIndex; }
    void onReset(uint32_t newFrame);
    void seekAfterFrame(uint32_t frame);
    void onExit();

    void advanceInputIndex() { m_inputIndex++; }
    bool hasFlippedControls() {
        return GameManager::get()->getGameVariable("0010");
    }
    bool playerFlipped(bool player) { return player ^ hasFlippedControls(); }

    uint64_t& getCurrentRandomState();
    uint64_t& getCurrentShakeState();

    [[nodiscard]] std::optional<slc::Action> peekQueuedInput() const {
        if (m_inputIndex >= m_actionAtom.m_actions.size()) return std::nullopt;
        return m_actionAtom.m_actions[m_inputIndex];
    }

    [[nodiscard]] const std::optional<slc::Action> getNextInput(uint32_t frame);

    void load(std::filesystem::path path);
    void save(std::filesystem::path path, bool noOverwrite = false);
    geode::Result<size_t> loadSupported(std::filesystem::path path);
    geode::Result<size_t> convertAndPlay(std::filesystem::path path);
    static geode::utils::file::FilePickOptions converterFileOptions();

    void merge(std::filesystem::path path);

    std::filesystem::path getCurrentPath();
    void backupExisting(std::filesystem::path path);
    void createBackup();
};

#endif
