#include "system.hpp"
#include "util/paths.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <slc/formats/v3/replay.hpp>
#include <vector>

#include "bot/bot.hpp"
#include "bot/updater.hpp"

using namespace geode::prelude;

void ReplaySystem::onReset(uint32_t newFrame) {
    if (Bot::get()->isRecording()) {
        m_actionAtom.clipActions(newFrame);

        m_inputIndex = m_actionAtom.length();
    } else {
        if (m_actionAtom.m_actions.empty()) {
            m_inputIndex = 0;
            return;
        }

        m_inputIndex =
            std::distance(m_actionAtom.m_actions.begin(),
                          std::find_if(m_actionAtom.m_actions.begin(),
                                       m_actionAtom.m_actions.end(),
                                       [newFrame](const auto i) -> bool {
                                           return i.m_frame >= newFrame;
                                       }));
    }
}

void ReplaySystem::seekAfterFrame(uint32_t frame) {
    m_inputIndex = static_cast<size_t>(std::distance(
        m_actionAtom.m_actions.begin(),
        std::upper_bound(
            m_actionAtom.m_actions.begin(), m_actionAtom.m_actions.end(), frame,
            [](uint32_t target, const slc::Action& action) {
                return target < action.m_frame;
            })));
}

[[nodiscard]] const std::optional<slc::Action> ReplaySystem::getNextInput(
    uint32_t frame) {
    if (m_inputIndex >= m_actionAtom.length()) {
        return std::nullopt;
    }

    auto& input = m_actionAtom.m_actions.at(m_inputIndex);
#ifdef GEODE_IS_MOBILE
    // Mobile advances the replay clock in logical TPS-sized steps because it
    // cannot install the desktop physics-step hook. Deliver every action up
    // to that step so short taps are not stranded between display frames.
    if (input.m_frame <= frame) {
#else
    if (input.m_frame == frame) {
#endif
        m_inputIndex++;
        return input;
    }

    return std::nullopt;
}

uint64_t& ReplaySystem::getCurrentRandomState() {
#ifndef GEODE_IS_WINDOWS
    return m_portableRandomState;
#else
    return *reinterpret_cast<uint64_t*>(geode::base::get() + 0x6c2e90);
#endif
}

uint64_t& ReplaySystem::getCurrentShakeState() {
    return this->m_shakeRandomState;
}

void ReplaySystem::onExit() { m_inputIndex = 0; }

using Replay = slc::v3::Replay<>;

void ReplaySystem::save(std::filesystem::path path, bool noOverwrite) {
    m_lastOperationSucceeded = false;
    if (noOverwrite && std::filesystem::exists(path)) {
        geode::log::info("Not overwriting replay at {}", path);
        return;
    }

    Replay replay;
    std::stable_sort(
        m_actionAtom.m_actions.begin(), m_actionAtom.m_actions.end(),
        [](const auto& a, const auto& b) { return a.m_frame < b.m_frame; });

    uint64_t previousFrame = 0;
    for (auto& action : m_actionAtom.m_actions) {
        // recalculate delta for all actions
        action.recalculateDelta(previousFrame);
        previousFrame = action.m_frame;
    }

    replay.m_atoms.add(m_actionAtom);

    replay.m_meta.m_build = 81;
    replay.m_meta.m_seed = m_startingSeed;
    replay.m_meta.m_tps = Bot::get()->updater().m_tps->inner();

    std::ofstream fd(path, std::ios::binary);
    if (!fd) {
        geode::log::error("Failed to open replay for writing: {}", path);
        return;
    }
    auto result = replay.write(fd);
    if (!result.has_value()) {
        geode::log::error("Failed to save replay: {}",
                          result.error().m_message);
        return;
    }

    geode::log::info("Successfully saved replay to {}", path);
    m_lastOperationSucceeded = true;
}

bool ReplaySystem::processSlc3(Replay& replay) {
    auto& atoms = replay.m_atoms.m_atoms;
    auto it = std::find_if(atoms.begin(), atoms.end(), [](auto& v) {
        return std::visit(
            [](auto& at) { return at.id == slc::v3::AtomId::Action; }, v);
    });

    if (it == atoms.end()) {
        geode::log::error("Replay contains no action data");
        return false;
    }

    auto atom = *it;
    auto& updater = Bot::get()->updater();
    m_actionAtom = std::get<slc::ActionAtom>(atom);
    std::stable_sort(
        m_actionAtom.m_actions.begin(), m_actionAtom.m_actions.end(),
        [](const auto& left, const auto& right) {
            return left.m_frame < right.m_frame;
        });
    m_inputIndex = 0;
    m_startingSeed = replay.m_meta.m_seed;
    m_startingSeedThisAttempt = m_startingSeed;
    m_lastInputs.clear();
    updater.resetFrame();
    updater.m_frameOnLastAttempt = 0;
    updater.m_expectsDeath = false;
    updater.m_fullReset = false;
    updater.m_tpsOverflow = 0.0;
    updater.m_tps->inner() =
        std::isfinite(replay.m_meta.m_tps) && replay.m_meta.m_tps > 0.0
            ? replay.m_meta.m_tps
            : 240.0;
    updater.m_tps->notifyChange();
    Bot::get()->setMode(Bot::Mode::Playing);
    return true;
}

bool ReplaySystem::processSlc2(slc::v2::Replay<ReplayMeta>& replay) {
    uint64_t currentFrame = 0;
    auto& a = m_actionAtom;
    a.clear();
    for (const auto& input : replay.getInputs()) {
        if (input.m_button == slc::v2::Input::InputType::Skip) {
            currentFrame = input.m_frame;
            continue;
        }

        if (static_cast<int>(input.m_button) <
            static_cast<int>(slc::v2::Input::InputType::Restart)) {
            a.m_actions.push_back(slc::v3::Action(
                currentFrame, input.m_frame - currentFrame,
                static_cast<slc::Action::ActionType>(input.m_button),
                input.m_holding, input.m_player2));
            currentFrame = input.m_frame;
            continue;
        }

        if (static_cast<int>(input.m_button) <
            static_cast<int>(slc::v2::Input::InputType::TPS)) {
            a.m_actions.push_back(slc::v3::Action(
                currentFrame, input.m_frame - currentFrame,
                static_cast<slc::v3::Action::ActionType>(input.m_button),
                replay.m_meta.seed));
            currentFrame = input.m_frame;
            continue;
        }

        a.m_actions.push_back(slc::v3::Action(
            currentFrame, input.m_frame - currentFrame, input.m_tps));
        currentFrame = input.m_frame;
    }

    auto& updater = Bot::get()->updater();
    std::stable_sort(
        m_actionAtom.m_actions.begin(), m_actionAtom.m_actions.end(),
        [](const auto& left, const auto& right) {
            return left.m_frame < right.m_frame;
        });
    m_inputIndex = 0;
    m_startingSeed = replay.m_meta.seed;
    m_startingSeedThisAttempt = m_startingSeed;
    m_lastInputs.clear();
    updater.resetFrame();
    updater.m_frameOnLastAttempt = 0;
    updater.m_expectsDeath = false;
    updater.m_fullReset = false;
    updater.m_tpsOverflow = 0.0;
    updater.m_tps->inner() =
        std::isfinite(replay.m_tps) && replay.m_tps > 0.0 ? replay.m_tps
                                                          : 240.0;
    updater.m_tps->notifyChange();
    Bot::get()->setMode(Bot::Mode::Playing);
    return true;
}

void ReplaySystem::load(std::filesystem::path path) {
    m_lastOperationSucceeded = false;
    if (!std::filesystem::exists(path) && path.extension() == ".grape") {
        auto legacyPath = path;
        legacyPath.replace_extension(".slc");
        if (std::filesystem::exists(legacyPath)) path = legacyPath;
    }
    if (!std::filesystem::exists(path)) {
        geode::log::error(
            "Failed to load slc3 replay from {}; file does not exist", path);
        return;
    }

    std::ifstream fd(path, std::ios::binary);

    auto replay = Replay::read(fd);
    if (replay.has_value()) {
        geode::log::info("Loading slc3 replay from {}", path);
        m_lastOperationSucceeded = this->processSlc3(replay.value());
    } else {
        fd.seekg(0, std::ios::beg);
        auto v2Replay = slc::v2::Replay<ReplayMeta>::read(fd);
        if (v2Replay.has_value()) {
            geode::log::info("Loading slc2 (legacy) replay from {}", path);
            m_lastOperationSucceeded = this->processSlc2(v2Replay.value());
        } else {
            geode::log::error("Failed to load slc3 replay from {}", path);
        }
    }
}

void ReplaySystem::merge(std::filesystem::path path, MergeMode mode) {
    if (!std::filesystem::exists(path)) {
        geode::log::error("Merge failed: file does not exist at {}", path);
        return;
    }

    // Load the other replay into a temporary atom
    std::ifstream fd(path, std::ios::binary);
    slc::ActionAtom otherAtom;

    auto replay = slc::v3::Replay<>::read(fd);
    if (replay.has_value()) {
        auto& atoms = replay.value().m_atoms.m_atoms;
        auto it = std::find_if(atoms.begin(), atoms.end(), [](auto& v) {
            return std::visit(
                [](auto& at) { return at.id == slc::v3::AtomId::Action; }, v);
        });
        if (it == atoms.end()) {
            geode::log::error("Merge failed: no action atom in other replay");
            return;
        }
        otherAtom = std::get<slc::ActionAtom>(*it);
    } else {
        fd.seekg(0, std::ios::beg);
        auto v2Replay = slc::v2::Replay<ReplayMeta>::read(fd);
        if (!v2Replay.has_value()) {
            geode::log::error("Merge failed: could not parse other replay");
            return;
        }
        // Convert v2 to v3 actions
        uint64_t currentFrame = 0;
        for (const auto& input : v2Replay.value().getInputs()) {
            if (input.m_button == slc::v2::Input::InputType::Skip) {
                currentFrame = input.m_frame;
                continue;
            }
            if (static_cast<int>(input.m_button) <
                static_cast<int>(slc::v2::Input::InputType::Restart)) {
                otherAtom.m_actions.push_back(slc::v3::Action(
                    currentFrame, input.m_frame - currentFrame,
                    static_cast<slc::Action::ActionType>(input.m_button),
                    input.m_holding, input.m_player2));
                currentFrame = input.m_frame;
            }
        }
    }

    // determine which player flag to strip from each source
    // current: we keep inputs where m_player2 != stripFromCurrent
    // other:   we keep inputs where m_player2 != stripFromOther (then optionally swap)
    bool keepCurrentP1 = true;  // keep P1 (m_player2==false) from current
    bool keepCurrentP2 = true;  // keep P2 (m_player2==true)  from current
    bool keepOtherP1   = true;
    bool keepOtherP2   = true;
    bool swapOther     = false;

    switch (mode) {
        case MergeMode::P1FromOther:
            keepCurrentP1 = false;
            keepOtherP2   = false;
            break;
        case MergeMode::P2FromOther:
            keepCurrentP2 = false;
            keepOtherP1   = false;
            break;
        case MergeMode::SwapPlayers:
            swapOther = true;
            break;
    }

    std::vector<slc::Action> merged;

    for (auto& action : m_actionAtom.m_actions) {
        bool isP2 = action.m_player2;
        if (isP2 && !keepCurrentP2) continue;
        if (!isP2 && !keepCurrentP1) continue;
        merged.push_back(action);
    }

    for (auto action : otherAtom.m_actions) {
        bool isP2 = action.m_player2;
        if (!swapOther) {
            if (isP2 && !keepOtherP2) continue;
            if (!isP2 && !keepOtherP1) continue;
        } else {
            action.m_player2 = !isP2;  // swap
        }
        merged.push_back(action);
    }

    // frame sort
    std::stable_sort(merged.begin(), merged.end(),
                     [](const auto& a, const auto& b) {
                         return a.m_frame < b.m_frame;
                     });

    uint64_t prevFrame = 0;
    for (auto& action : merged) {
        action.recalculateDelta(prevFrame);
        prevFrame = action.m_frame;
    }

    m_actionAtom.m_actions = std::move(merged);
	
	// don't works, anyway doesn't matter
    geode::log::info("Macro merge complete. Total inputs after merge: {}",
                     m_actionAtom.m_actions.size());
}

std::filesystem::path ReplaySystem::getCurrentPath() {
    return silicate::paths::directory("replays") /
           (m_replayName + ".grape");
}

static std::filesystem::path createBackupPath(const std::string& name) {
    namespace time = std::chrono;

    const time::time_point timestamp =
        time::floor<time::milliseconds>(time::system_clock::now());

    const std::filesystem::path path =
        silicate::paths::directory("backups") /
        fmt::format("_backup_{:%Y%m%d_%H%M%S}_{}.grape", timestamp, name);

    return path;
}

void ReplaySystem::backupExisting(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::string name = path.stem().string();
    auto newPath = createBackupPath(name);
    std::filesystem::copy(path, newPath,
                          std::filesystem::copy_options::skip_existing);
}

void ReplaySystem::createBackup() {
    auto path = createBackupPath(m_replayName);
    this->save(path, true);
}

$execute {
    auto bot = Bot::get();
    auto& rs = bot->replaySystem();

    rs.m_autosaveId = bot->scheduler().schedule(
        [&rs]() {
            auto pl = PlayLayer::get();
            if (!pl) return;
            if (!rs.m_autosaveAtInterval->inner()) return;

            if (Bot::get()->isRecording()) {
                rs.createBackup();
            }
        },
        rs.m_autosaveInterval->inner(), true);

    rs.m_autosaveInterval->handle([bot, &rs](double& interval) {
        bot->scheduler().reschedule(rs.m_autosaveId, interval);
    });
}
