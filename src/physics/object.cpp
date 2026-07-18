#include "object.hpp"

#include <Geode/Geode.hpp>

namespace phys {
bool activatedPlatformer(EnhancedGameObject* object, bool isPlatformer) {
    if (!isPlatformer) {
        if (object->m_isMultiActivate) {
            return true;
        }
    } else if (object->m_isNoMultiActivate) {
        return true;
    }

    return false;
}

void* hasBeenActivatedByPlayerOrig = nullptr;

$execute {
    hasBeenActivatedByPlayerOrig =
        reinterpret_cast<void*>(geode::base::get() + 0x1a1b70);
}

bool hasBeenActivatedByPlayer(PlayerObject* player,
                              EnhancedGameObject* object) {
    bool activatedPlatformer =
        phys::activatedPlatformer(object, player->m_isPlatformer);
    if (activatedPlatformer) {
        return false;
    }

    if (!player->m_isSecondPlayer) {
        return object->m_activatedByPlayer1;
    }

    return object->m_activatedByPlayer2;

}
}
