#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <deque>

#include "settings/settings.hpp"
#include "shared/value/value.hpp"

struct HitboxTrailUnit {
    cocos2d::CCRect m_rect;
    cocos2d::CCRect m_scaled;
    std::array<cocos2d::CCPoint, 4> m_rotated;

    inline bool shouldDraw(float minX, float maxX, float minY,
                           float maxY) const noexcept {
        return !(m_rect.getMaxX() < minX || m_rect.getMinX() > maxX ||
                 m_rect.getMinY() > maxY || m_rect.getMaxY() < minY);
    }
};

class Hitboxes {
    class HitboxesDrawNode : public cocos2d::CCDrawNode {
       public:
        static HitboxesDrawNode* create() {
            auto* node = new HitboxesDrawNode();
            if (node && node->init()) {
                node->autorelease();
                node->m_bUseArea = false;
            } else {
                CC_SAFE_DELETE(node);
            }
            return node;
        }
    };

    HitboxesDrawNode* m_drawNode       = nullptr;
    HitboxesDrawNode* m_trailDrawNode  = nullptr;

    bool _enabled = false;

    std::deque<HitboxTrailUnit> m_trailP1;
    std::deque<HitboxTrailUnit> m_trailP2;

    bool m_trailDirty = false;

    int m_trailStepCounter = 0;

    void safeRelease(HitboxesDrawNode*& node);

   public:
    bool m_initialized = false;

    SLValuePtr<bool> m_enabled =
        SLValue<bool>::create("hitboxes.enabled", &_enabled);
    SLValuePtr<bool> m_trailEnabled = SLValue<bool>::create(
        "hitboxes.trail_enabled", &SLSettings::get()->hitboxes.trailEnabled);
    SLValuePtr<double> m_width = SLValue<double>::create(
        "hitboxes.width", &SLSettings::get()->hitboxes.width);

    void init(GJBaseGameLayer* pl);
    void clearTrail();
    void saveToTrail(GJBaseGameLayer* pl);
    void draw(GJBaseGameLayer* pl);
    void destroy();
};
