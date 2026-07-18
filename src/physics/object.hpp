#pragma once

#include <Geode/Geode.hpp>

namespace phys {
bool activatedPlatformer(EnhancedGameObject* object, bool isPlatformer);

bool hasBeenActivatedByPlayer(PlayerObject* player, EnhancedGameObject* object);
}
