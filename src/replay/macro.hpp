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
        "replay.autosave", &GrapeSettings::get()->autosaveAtLevelEnd);

    ConfigValuePtr<bool> m_useAlternateHook = ConfigValue<bool>::create(
        "replay.althook", &GrapeSettings::get()->useAlternateHook);

    bool m_overrideSeed = false;
    uint64_t m_overriddenSeed = 0;
    uint64_t m_startingSeed = 0;
    uint64_t m_startingSeedThisAttempt = 0;
    uint64_t m_shakeRandomState = 0;
#ifndef GEODE_IS_WINDOWS
    uint64_t m_portableRandomState = 0;
#endif
    bool m_flipProcessingInputs = false;

    bool m_forceNextInput = false;
    std::unordered_map<int, slc::Action> m_lastInputs;

    bool m_mirrorInputs = false;
    bool m_mirrorInverted = false;
    bool m_maintainGravity = false;

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

    [[nodiscard]] const std::optional<slc::Action> getNextQueuedInput() {
        if (m_inputIndex >= m_actionAtom.m_actions.size())
            return std::nullopt;

        return m_actionAtom.m_actions.at(m_inputIndex);
    }

    [[nodiscard]] const std::optional<slc::Action> getCurrentQueuedInput() {
        if (m_inputIndex >= m_actionAtom.m_actions.size()) return std::nullopt;

        return m_actionAtom.m_actions.at(m_inputIndex);
    }

    [[nodiscard]] const std::optional<slc::Action> getNextInput(uint32_t frame);

    void load(std::filesystem::path path);
    void save(std::filesystem::path path, bool noOverwrite = false);
    geode::Result<size_t> convertAndPlay(std::filesystem::path path);
    static geode::utils::file::FilePickOptions converterFileOptions();

    enum class MergeMode { P1FromOther, P2FromOther, SwapPlayers };
    void merge(std::filesystem::path path, MergeMode mode = MergeMode::P2FromOther);

    std::filesystem::path getCurrentPath();
    void backupExisting(std::filesystem::path path);
    void createBackup();
};

#endif
