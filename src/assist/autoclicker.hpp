#pragma once
#include <Geode/Geode.hpp>

#include "../settings/settings.hpp"
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
    uint64_t m_lastFrame = UINT64_MAX;
    bool m_p1Clicked = false;
    bool m_p2Clicked = false;

   public:
    int m_frequency = SLSettings::get()->autoclickerFrequency;
    bool m_performSwifts = SLSettings::get()->autoclickerSwifts;
    SLValuePtr<bool> m_enabled = SLValue<bool>::create(
        "autoclicker.enabled", &SLSettings::get()->autoclickerEnabled);
    PlayerToggle m_player = static_cast<PlayerToggle>(
        std::clamp(SLSettings::get()->autoclickerPlayer, 0, 2));

    void saveSettings() {
        m_frequency = std::max(1, m_frequency);
        SLSettings::get()->autoclickerFrequency = m_frequency;
        SLSettings::get()->autoclickerSwifts = m_performSwifts;
        SLSettings::get()->autoclickerPlayer = static_cast<int>(m_player);
    }

    void reset() {
        m_lastFrame = UINT64_MAX;
        m_p1Clicked = false;
        m_p2Clicked = false;
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
