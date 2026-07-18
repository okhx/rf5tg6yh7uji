#include <Geode/Geode.hpp>

#include "bot/bot.hpp"
#include "bot/scheduler.hpp"
#include "bot/updater.hpp"
#include "render/renderer.hpp"

using namespace geode::prelude;

#include <Geode/modify/CCDirector.hpp>

struct SLCCDirector : Modify<SLCCDirector, CCDirector> {
    void drawScene() {
        if (!Bot::get()->isEnabled()) {
            return CCDirector::drawScene();
        }

        Bot::get()->scheduler().update(this->getDeltaTime());

        auto renderer = Renderer::get();
        Bot::get()->updater().updateAudioSpeedhack();

        auto playLayer = PlayLayer::get();
        if (!playLayer) {
            return CCDirector::drawScene();
        }

#ifdef GEODE_IS_MOBILE
        if (renderer->isRecording()) {
            CCDirector::drawScene();
            renderer->update(playLayer);
            Bot::get()->updater().runFrozenTick();
            return;
        }
#endif

        if (renderer->isRecording() && !playLayer->m_isPaused &&
            (playLayer->m_started ||
             renderer->m_settings.m_firstAttemptPause)) {
            float dt = Renderer::get()->getDt();

            SL_LOG_DEV("Checking if renderer collected in drawScene...");
            if (renderer->m_halting) {
                SL_LOG_DEV("Collected so not updating yet...");
                renderer->displayPreview();
                this->m_pobOpenGLView->swapBuffers();

                Bot::get()->updater().runFrozenTick();
                return;
            }

            SL_LOG_DEV("Updating because not collected yet...");

            if (!m_bPaused) {
                m_pScheduler->update(dt);
            }

            SL_LOG_DEV("Scheduler updated, setting next scene...");

            if (m_pNextScene) {
                this->setNextScene();
            }

            renderer->update(playLayer);

            renderer->displayPreview();
            this->m_pobOpenGLView->swapBuffers();

            Bot::get()->updater().runFrozenTick();

            return;
        }
        CCDirector::drawScene();
        Bot::get()->updater().runFrozenTick();
    }

};
