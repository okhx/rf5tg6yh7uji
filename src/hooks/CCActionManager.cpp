#include <Geode/Geode.hpp>
#include <Geode/modify/CCActionManager.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"

using namespace geode::prelude;

class $modify(GrapeCCActionManager, CCActionManager) {
    void update(float dt) {
        auto& timeline = GrapeEngine::get()->timeline();
        timeline.m_actionMgr = this;
        if (!GrapeEngine::get()->isEnabled() || !timeline.m_onlyRefresh)
            CCActionManager::update(dt);
    }
};
