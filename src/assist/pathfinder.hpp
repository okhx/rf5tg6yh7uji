#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>
#include <cstdint>

#include "../config/config.hpp"
#include "../shared/value/value.hpp"

using namespace geode::prelude;

class Trajectory;

// Vision-based auto-player. Each frame it look-ahead-simulates the player's
// trajectory for both "hold" and "release" inputs (the same simulation the
// trajectory renderer and Find Best Frame use) and picks whichever survives
// longer, clicking to stay alive -- a generalized Moving Gap Assist that "sees"
// hazards, blocks and the player's trajectory across the whole level rather
// than just the next gap. On death the level restarts from the last checkpoint;
// when it keeps dying at the same spot it drops that checkpoint so the retry
// resumes from earlier and can approach the section differently.
class Pathfinder {
   public:
    ConfigValuePtr<bool> m_enabled = ConfigValue<bool>::create(
        "pathfinder.enabled", &GrapeSettings::get()->pathfinderEnabled);
    int m_stuckDeaths =
        std::max(1, GrapeSettings::get()->pathfinderStuckDeaths);

   private:
    uint64_t m_lastFrame = UINT64_MAX;
    bool m_p1Clicked = false;
    bool m_p2Clicked = false;
    uint64_t m_p1LastToggle = UINT64_MAX;
    uint64_t m_p2LastToggle = UINT64_MAX;
    float m_p1SwiftAcc = 0.0f;
    float m_p2SwiftAcc = 0.0f;

    uint64_t m_lastDeathFrame = UINT64_MAX;
    int m_consecutiveDeaths = 0;

    // Guards the instant respawn so it fires once per death.
    bool m_awaitingRespawn = false;

    void decide(PlayLayer* pl, Trajectory* traj, bool p1, bool& clicked);

   public:
    bool m_proportional = GrapeSettings::get()->autoclickerProportional;
    float m_holdPct = GrapeSettings::get()->autoclickerHoldPct;
    float m_releasePct = GrapeSettings::get()->autoclickerReleasePct;
    float m_swiftPct = GrapeSettings::get()->autoclickerSwiftPct;
    int m_cycleFrames =
        std::max(2, GrapeSettings::get()->autoclickerCycleFrames);

    // Driven once per physics frame from the frame-update hook.
    void update(PlayLayer* pl);

    // Called from PlayLayer::resetLevel with the frame the player died on, so
    // the learner can track "stuck" spots and drop checkpoints when needed.
    void onDeath(PlayLayer* pl, uint64_t deathFrame);

    void reset() {
        m_lastFrame = UINT64_MAX;
        m_p1Clicked = false;
        m_p2Clicked = false;
        m_p1LastToggle = UINT64_MAX;
        m_p2LastToggle = UINT64_MAX;
        m_p1SwiftAcc = 0.0f;
        m_p2SwiftAcc = 0.0f;
        m_awaitingRespawn = false;
    }

    void saveSettings() {
        m_stuckDeaths = std::max(1, m_stuckDeaths);
        GrapeSettings::get()->pathfinderStuckDeaths = m_stuckDeaths;
        const float sum = m_holdPct + m_releasePct + m_swiftPct;
        if (sum > 0.0f) {
            m_holdPct = m_holdPct / sum * 100.0f;
            m_releasePct = m_releasePct / sum * 100.0f;
            m_swiftPct = m_swiftPct / sum * 100.0f;
        } else {
            m_holdPct = m_releasePct = m_swiftPct = 100.0f / 3.0f;
        }
        m_cycleFrames = std::max(2, m_cycleFrames);
        GrapeSettings::get()->autoclickerProportional = m_proportional;
        GrapeSettings::get()->autoclickerHoldPct = m_holdPct;
        GrapeSettings::get()->autoclickerReleasePct = m_releasePct;
        GrapeSettings::get()->autoclickerSwiftPct = m_swiftPct;
        GrapeSettings::get()->autoclickerCycleFrames = m_cycleFrames;
    }
};
