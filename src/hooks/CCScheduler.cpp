#include <Geode/Geode.hpp>

#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "render/renderer.hpp"
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
        // High-resolution readback and encoding can make the wall-clock delta
        // spike. Clamp gameplay itself to one output-frame step so a slow iOS
        // encode cannot jump the macro forward and create catch-up bursts.
        if (auto* renderer = Renderer::get(); renderer->isRecording()) {
            dt = renderer->getDt();
        }
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
