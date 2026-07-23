#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"

using namespace geode::prelude;

#include <Geode/modify/CCActionManager.hpp>

#ifdef GEODE_IS_WINDOWS
constexpr int ACTIONMGR_UPDATE_OFFSET = 0x38B90;
static void* _CCActionManager_update_orig;

inline static void CCActionManager_update_orig(CCActionManager* self,
                                               float dt) {
    reinterpret_cast<void (*)(cocos2d::CCActionManager*, float)>(
        _CCActionManager_update_orig)(self, dt);
}

void CCActionManager_update(cocos2d::CCActionManager* mgr, float dt) {
    GrapeEngine::get()->timeline().m_actionMgr = mgr;
    if (!GrapeEngine::get()->isEnabled()) {
        CCActionManager_update_orig(mgr, dt);
        return;
    }
    if (GrapeEngine::get()->timeline().m_onlyRefresh) {
        return;
    }

    CCActionManager_update_orig(mgr, dt);
}

$execute {
    _CCActionManager_update_orig = reinterpret_cast<void*>(
        geode::base::getCocos() + ACTIONMGR_UPDATE_OFFSET);
    (void)Mod::get()->hook(_CCActionManager_update_orig,
                           &CCActionManager_update, "CCActionManager::update",
                           tulip::hook::TulipConvention::Fastcall);
}
#endif
