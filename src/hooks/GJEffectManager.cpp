#include <Geode/Geode.hpp>

using namespace geode::prelude;

#include <Geode/modify/GJEffectManager.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"

struct GrapeGJEffectManager : Modify<GrapeGJEffectManager, GJEffectManager> {
    void updateEffects(float dt) {
        if (GrapeEngine::get()->isEnabled() &&
            GrapeEngine::get()->timeline().m_layoutMode->inner() &&
            !LevelEditorLayer::get()) {
            m_pulseEffectMap.clear();
            m_pulseEffectVector.clear();
            m_opacityEffectMap.clear();
        }

        GJEffectManager::updateEffects(dt);
    }
};
