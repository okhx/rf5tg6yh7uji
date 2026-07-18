#include <Geode/Geode.hpp>

using namespace geode::prelude;

#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/GameObject.hpp>

#include "bot/bot.hpp"
#include "bot/updater.hpp"

static const std::unordered_set<int> OTHER_DECO_IDS = {
    902,  943,  944,  945,  946,  947,  948,  949,  950,  951,
    980,  981,  982,  983,  984,  985,  986,  987,  988,  1023,
    1024, 1025, 1026, 1027, 1028, 1029, 1030, 1031, 1032, 1063,
    1064, 1065, 1066, 1067, 1068, 1069, 1070, 1071};

struct SLGameObject : Modify<SLGameObject, GameObject> {
    bool isNoHitboxObject() {
        auto rect = this->getObjectRect();

        return rect.getMinX() == rect.getMaxX() &&
               rect.getMinY() == rect.getMaxY();
    }

    void addGlow(gd::string p0) {
        GameObject::addGlow(std::move(p0));

        if (LevelEditorLayer::get()) return;

        if (Bot::get()->updater().m_layoutMode->inner()) {
            if (m_objectType == GameObjectType::Decoration || m_isNoTouch ||
                (m_objectID >= 506 && m_objectID <= 640) ||
                OTHER_DECO_IDS.contains(m_objectID)) {
                m_isHide = true;
            } else {
                m_isHide = false;
                m_isDontFade = true;
                m_isDontEnter = true;

                if (m_isPassable) {
                    m_opacityMod = 0.5;
                    m_opacityMod2 = 0.5;
                }
            }
        }
    }
};
