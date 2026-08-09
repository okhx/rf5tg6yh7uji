#pragma once
#include <Geode/Geode.hpp>

#include <limits>

#include "../config/config.hpp"
#include "../shared/value/value.hpp"

using namespace geode::prelude;

class Autoclicker {
   public:
    enum class PlayerToggle : int {
        Player1,
        Player2,
        Both,
    };

   private:
    struct GapState {
        float center = std::numeric_limits<float>::quiet_NaN();
        float lower = std::numeric_limits<float>::quiet_NaN();
        float upper = std::numeric_limits<float>::quiet_NaN();
        float velocity = 0.0f;
        float acceleration = 0.0f;
        uint64_t frame = UINT64_MAX;
    };

    uint64_t m_lastFrame = UINT64_MAX;
    uint64_t m_p1LastToggle = UINT64_MAX;
    uint64_t m_p2LastToggle = UINT64_MAX;
    bool m_p1Clicked = false;
    bool m_p2Clicked = false;
    GapState m_p1Gap;
    GapState m_p2Gap;
   public:
    int m_holdFrames = GrapeSettings::get()->autoclickerHoldFrames;
    int m_releaseFrames = GrapeSettings::get()->autoclickerReleaseFrames;
    bool m_performSwifts = GrapeSettings::get()->autoclickerSwifts;
    bool m_movingGap = GrapeSettings::get()->autoclickerMovingGap;
    int m_movingGapLookahead = std::clamp(
        GrapeSettings::get()->autoclickerMovingGapLookahead, 1, 30);

    ConfigValuePtr<bool> m_enabled = ConfigValue<bool>::create(
        "autoclicker.enabled", &GrapeSettings::get()->autoclickerEnabled);
    PlayerToggle m_player = static_cast<PlayerToggle>(
        std::clamp(GrapeSettings::get()->autoclickerPlayer, 0, 2));

    void saveSettings() {
        m_holdFrames = std::max(1, m_holdFrames);
        m_releaseFrames = std::max(1, m_releaseFrames);
        GrapeSettings::get()->autoclickerHoldFrames = m_holdFrames;
        GrapeSettings::get()->autoclickerReleaseFrames = m_releaseFrames;
        GrapeSettings::get()->autoclickerSwifts = m_performSwifts;
        GrapeSettings::get()->autoclickerMovingGap = m_movingGap;
        m_movingGapLookahead = std::clamp(m_movingGapLookahead, 1, 30);
        GrapeSettings::get()->autoclickerMovingGapLookahead =
            m_movingGapLookahead;
        GrapeSettings::get()->autoclickerPlayer = static_cast<int>(m_player);

    }

    void reset() {
        m_lastFrame = UINT64_MAX;
        m_p1LastToggle = UINT64_MAX;
        m_p2LastToggle = UINT64_MAX;
        m_p1Clicked = false;
        m_p2Clicked = false;
        m_p1Gap = {};
        m_p2Gap = {};
    }
    void update(PlayLayer* pl);
    bool performPlayer1() {
        return m_player == PlayerToggle::Player1 ||
               m_player == PlayerToggle::Both;
    }
    bool performPlayer2() {
        return m_player == PlayerToggle::Player2 ||
               m_player == PlayerToggle::Both;
    }
};
