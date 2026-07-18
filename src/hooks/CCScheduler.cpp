#include <Geode/Geode.hpp>

#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "render/renderer.hpp"
#include "ui/touch_overlay.hpp"

using namespace geode::prelude;

#include <Geode/modify/CCScheduler.hpp>

struct SLCCScheduler : Modify<SLCCScheduler, CCScheduler> {
    void update(float dt) override {
#ifdef GEODE_IS_MOBILE
        TouchOverlay::get()->update(dt);
        if (dt > 0.0f) {
            auto* renderer = Renderer::get();
            if (renderer->isRecording())
                dt = renderer->getDt();
        }
#endif
        const auto bot = Bot::get();
        if (bot->updater().m_onlyRefresh || !bot->isEnabled()) {
            CCScheduler::update(dt);
            return;
        }

        bot->updater().runUpdates(
            [this](float dt) { this->CCScheduler::update(dt); }, dt, false);
    }
};
