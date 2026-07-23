
#ifndef FIX_HPP
#define FIX_HPP

#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "Geode/binding/CheckpointGameObject.hpp"
#include "checkpoint.hpp"
#include "config/config.hpp"
#include "shared/value/value.hpp"

class PracticeFix {
   private:
   public:
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

    template <class Container>
    void updatePlatformerInputs(const Container& inputs) {
        for (const auto& input : inputs) {
            bool& left = input.m_isPlayer2 ? m_p2Left : m_p1Left;
            bool& right = input.m_isPlayer2 ? m_p2Right : m_p1Right;

            if (input.m_button == PlayerButton::Left) {
                left = input.m_isPush;
            } else if (input.m_button == PlayerButton::Right) {
                right = input.m_isPush;
            }
        }
    }
    void registerBrokenObject(GameObject* obj) {
        m_brokenObjects.push_back(obj);
    }

    bool m_p1Left = false;
    bool m_p1Right = false;

    bool m_p2Left = false;
    bool m_p2Right = false;

    bool m_shouldLoadPlatformer = false;
};

#endif
