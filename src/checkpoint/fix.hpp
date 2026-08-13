//
// Created by peony on 29.10.2024.
//

#ifndef FIX_HPP
#define FIX_HPP

#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "Geode/binding/CheckpointGameObject.hpp"
#include "Geode/utils/function.hpp"
#include "checkpoint.hpp"
#include "config/config.hpp"
#include "shared/value/value.hpp"

class PlayLayer;

// True while a practice checkpoint must not be created.
//
// A checkpoint snapshots the current frame, the replay input index and the RNG
// states (see PracticeFix::createCheckpoint). Taking one after the player has
// died records a moment the run never actually played through, so restoring it
// resumes the macro from an input index that doesn't line up with the frame and
// the macro desyncs. Every placement path has to go through this.
//
// Both the level's death flag and each player's are checked because they aren't
// set at the same moment: PlayLayer::m_playerDied covers the death-to-respawn
// window, while m_isDead is per-player (and player 2 may not exist).
bool checkpointPlacementBlocked(PlayLayer* pl);

class PracticeFix {
   private:
   public:
    struct SavedAdvRand {
        uint64_t* m_randomState;
        int m_uniqueID;
    };

    ConfigValuePtr<uint32_t> m_maxStoredFrames = ConfigValue<uint32_t>::create(
        "practice_fix.max_stored_frames", &GrapeSettings::get()->stepsToSave);

    SavedCheckpoint* m_forcedState;

    bool m_loadCheckpoint = false;

    std::deque<SavedCheckpoint> m_storedFrames;
    bool m_hasDiedNormally = false;
    bool m_isBackstep = false;
    std::vector<SavedCheckpoint> m_savedCheckpoints;
    std::deque<std::pair<CheckpointObject*, CheckpointGameObject*>>
        m_platformerCheckpoints;

    std::vector<SavedAdvRand> m_advancedRandom;

    // CheckpointObject* m_platformerCheckpoint = nullptr;
    // CheckpointGameObject* m_platformerCheckpointGame = nullptr;

    std::vector<GameObject*> m_brokenObjects;

    SavedCheckpoint createCheckpoint(CheckpointObject* obj,
                                     uint64_t attemptStartFrame);
    void applyCheckpoint(SavedCheckpoint& cp);

    bool canRestoreState();
    void saveState(CheckpointObject* obj, uint64_t attemptStartFrame);
    void clearStoredFrames();
    void restorePreviousFrame(std::function<void(CheckpointObject*)> loadFn);
    void resetWithState(const SavedCheckpoint& state);
    void dropLastStoredFrame();

    void saveCurrent(CheckpointObject* obj, uint64_t attemptStartFrame);
    void applyLatest();
    void popLatest();
    void removeAll();
    void clearPlatformer(bool assumeLoaded = false);

    void reseedAdvancedRandom(uint64_t attemptSeed);

    void updatePlatformerInputs(gd::vector<PlayerButtonCommand>& inputs);
    void registerBrokenObject(GameObject* obj) {
        m_brokenObjects.push_back(obj);
    }

    bool m_p1Left = false;
    bool m_p1Right = false;

    bool m_p2Left = false;
    bool m_p2Right = false;

    bool m_shouldLoadPlatformer = false;
};

#endif  // FIX_HPP
