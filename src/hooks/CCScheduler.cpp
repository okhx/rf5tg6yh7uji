#include <Geode/Geode.hpp>

#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "ui/touch_overlay.hpp"

using namespace geode::prelude;

#include <Geode/modify/CCScheduler.hpp>

struct SLCCScheduler : Modify<SLCCScheduler, CCScheduler> {
    void update(float dt) override {
        // Frame advance deliberately freezes the Cocos scheduler. Tick the
        // mobile step controls outside that freeze so press-and-hold can
        // continue producing steps while the level itself stays paused.
#ifdef GEODE_IS_MOBILE
        TouchOverlay::get()->update(dt);
#endif
        const auto bot = Bot::get();
        if (bot->updater().m_onlyRefresh || !bot->isEnabled()) {
            CCScheduler::update(dt);  // only update at the cocos level
            return;
        }

        bot->updater().runUpdates(
            [this](float dt) { this->CCScheduler::update(dt); }, dt, false);
    }
};
