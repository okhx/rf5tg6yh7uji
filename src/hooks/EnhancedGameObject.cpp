#include <Geode/Geode.hpp>
#include <engine/timeline.hpp>
#ifdef GEODE_IS_WINDOWS
#include <safetyhook.hpp>
#endif

#include "engine/engine.hpp"
#include "physics/collisions.hpp"
#include "trajectory/trajectory.hpp"

using namespace geode::prelude;

#include <Geode/modify/EnhancedGameObject.hpp>

struct GrapeEnhancedGameObject : Modify<GrapeEnhancedGameObject, EnhancedGameObject> {
    void activatedByPlayer(PlayerObject* player) {
        auto bot = GrapeEngine::get();
        if (bot->trajectory().isFakePlayer(player)) {
            phys::activateForTrajectory((EffectGameObject*)this, player);
        } else {
            EnhancedGameObject::activatedByPlayer(player);

        }
    }
};
