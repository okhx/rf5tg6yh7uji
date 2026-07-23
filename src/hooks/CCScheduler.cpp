#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"
#ifdef GEODE_IS_MOBILE
#include "ui/touch_overlay.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/CCScheduler.hpp>

struct GrapeCCScheduler : Modify<GrapeCCScheduler, CCScheduler> {
    void update(float dt) override {
#ifdef GEODE_IS_MOBILE
        TouchOverlay::get()->update(dt);
        if (dt > 0.0f) {
            auto* renderer = Renderer::get();
            if (renderer->isRecording())
                dt = renderer->getDt();
        }
#endif
        const auto bot = GrapeEngine::get();
        if (bot->timeline().m_onlyRefresh || !bot->isEnabled()) {
            CCScheduler::update(dt);
            return;
        }

        bot->timeline().runUpdates(
            [this](float dt) { this->CCScheduler::update(dt); }, dt, false);
    }
};
