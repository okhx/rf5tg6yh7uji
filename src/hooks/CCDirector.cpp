#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "engine/clock.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"

using namespace geode::prelude;

#include <Geode/modify/CCDirector.hpp>

struct GrapeCCDirector : Modify<GrapeCCDirector, CCDirector> {
    void drawScene() {
        if (!GrapeEngine::get()->isEnabled()) {
            return CCDirector::drawScene();
        }

        GrapeEngine::get()->clock().update(this->getDeltaTime());

        auto renderer = Renderer::get();
        GrapeEngine::get()->timeline().updateAudioSpeedhack();

        auto playLayer = PlayLayer::get();
        if (!playLayer) {
            return CCDirector::drawScene();
        }

#ifdef GEODE_IS_MOBILE
        if (renderer->isRecording()) {
            CCDirector::drawScene();
            renderer->update(playLayer);
            GrapeEngine::get()->timeline().runFrozenTick();
            return;
        }
#endif

        if (renderer->isRecording() && !playLayer->m_isPaused &&
            (playLayer->m_started ||
             renderer->m_settings.m_firstAttemptPause)) {
            float dt = Renderer::get()->getDt();

            GRAPE_LOG_DEV("Checking if renderer collected in drawScene...");
            if (renderer->m_halting) {
                GRAPE_LOG_DEV("Collected so not updating yet...");
                renderer->displayPreview();
                this->m_pobOpenGLView->swapBuffers();

                GrapeEngine::get()->timeline().runFrozenTick();
                return;
            }

            GRAPE_LOG_DEV("Updating because not collected yet...");

            if (!m_bPaused) {
                m_pScheduler->update(dt);
            }

            GRAPE_LOG_DEV("Scheduler updated, setting next scene...");

            if (m_pNextScene) {
                this->setNextScene();
            }

            renderer->update(playLayer);

            renderer->displayPreview();
            this->m_pobOpenGLView->swapBuffers();

            GrapeEngine::get()->timeline().runFrozenTick();

            return;
        }
        CCDirector::drawScene();
        GrapeEngine::get()->timeline().runFrozenTick();
    }

};
