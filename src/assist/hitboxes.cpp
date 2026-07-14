#include "hitboxes.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>

#include "bot/bot.hpp"
#include "ccTypes.h"
#include "settings/settings.hpp"
#include "trajectory/trajectory.hpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#define CC_COLOR(color_type)                \
    *reinterpret_cast<cocos2d::ccColor4F*>( \
        settings.categories[color_type].colors.data())
#define CC_FILL_COLOR(color_type)                               \
    {                                                           \
        settings.categories[color_type].colors.data()[0],       \
        settings.categories[color_type].colors.data()[1],       \
        settings.categories[color_type].colors.data()[2],       \
        settings.categories[color_type].colors.data()[3] *      \
            (float)settings.categories[color_type].fillOpacity, \
    }
#define HB_ENABLED(_t) settings.categories[_t].enabled

static cocos2d::CCRect usingWidth(const cocos2d::CCRect& old,
                                  float width) noexcept {
    if (width == 0.0f) return old;
    cocos2d::CCRect r = old;
    r.origin.x    += width;
    r.origin.y    += width;
    r.size.width  -= width * 2.0f;
    r.size.height -= width * 2.0f;
    return r;
}

// Clamp circle segment count to keep GPU load reasonable.
static inline int circleSegments(float radius) noexcept {
    // Minimum 8, maximum 48 — more than enough for any on-screen circle.
    return static_cast<int>(std::clamp(radius * 1.5f, 8.0f, 48.0f));
}

// Cheaper variant for trail circles — half the segments is plenty.
static inline int trailCircleSegments(float radius) noexcept {
    return static_cast<int>(std::clamp(radius * 0.75f, 6.0f, 20.0f));
}

// ---------------------------------------------------------------------------
// Resolved category helpers
// ---------------------------------------------------------------------------

struct ResolvedHBCategory {
    bool               enabled;
    cocos2d::ccColor4F line;
    cocos2d::ccColor4F fill;
};

struct ResolvedObjectHB {
    float               width;
    ResolvedHBCategory  interactable;
    ResolvedHBCategory  interactableActive;
    ResolvedHBCategory  solid;
    ResolvedHBCategory  passable;
    ResolvedHBCategory  hazard;
};

static ResolvedHBCategory resolveHB(SLSettings::HitboxSettings& s,
                                    int type) noexcept {
    auto& c = s.categories[type];
    const float a = c.colors[3];
    return {
        .enabled = c.enabled,
        .line    = {c.colors[0], c.colors[1], c.colors[2], a},
        .fill    = {c.colors[0], c.colors[1], c.colors[2],
                    a * static_cast<float>(c.fillOpacity)},
    };
}

// ---------------------------------------------------------------------------
// Player hitbox
// ---------------------------------------------------------------------------

static void drawPlayerHitbox(cocos2d::CCDrawNode*       node,
                             GJBaseGameLayer*            pl,
                             PlayerObject*               player,
                             SLSettings::HitboxSettings& settings) {
    using Type = SLSettings::HitboxSettings::Type;
    if (!player) return;

    const float width  = settings.width / pl->m_gameState.m_cameraZoom;
    const auto  rect   = usingWidth(player->getObjectRect(), width);
    const auto  scaled = usingWidth(player->getObjectRect(0.3f, 0.3f), width);

    if (auto ob = player->getOrientedBox();
        ob && HB_ENABLED(Type::PlayerRotated)) {
        node->drawPolygon(ob->m_corners.data(), 4,
                          CC_FILL_COLOR(Type::PlayerRotated), width,
                          CC_COLOR(Type::PlayerRotated));
    }

    if (HB_ENABLED(Type::PlayerCircle)) {
        float radius = player->getObjectRect().size.width * 0.5f;
        if (radius > 0.0f) {
            node->drawCircle(player->getPosition(), radius,
                             {0.0f, 0.0f, 0.0f, 0.0f}, width,
                             CC_COLOR(Type::PlayerCircle),
                             circleSegments(radius));
        }
    }

    if (HB_ENABLED(Type::Player))
        node->drawRect(rect, CC_FILL_COLOR(Type::Player), width,
                       CC_COLOR(Type::Player));

    if (HB_ENABLED(Type::PlayerInner))
        node->drawRect(scaled, CC_FILL_COLOR(Type::PlayerInner), width,
                       CC_COLOR(Type::PlayerInner));
}

// ---------------------------------------------------------------------------
// Object hitbox
// ---------------------------------------------------------------------------

const std::array ALWAYS_ALLOWED_MODIFIERS = {200, 201, 202, 203,
                                             1334, 1816, 3643};

static void drawObjectHitbox(cocos2d::CCDrawNode*    node,
                             GJBaseGameLayer*         pl,
                             GameObject*              object,
                             const ResolvedObjectHB&  hb) {
    if (!object || object->m_isGroupDisabled || !object->m_isActivated ||
        object->m_objectType == GameObjectType::Decoration)
        return;

    if (object == pl->m_player1 || object == pl->m_player2 ||
        Bot::get()->trajectory().isFakePlayer((PlayerObject*)object))
        return;

    const float width = hb.width;

    switch (object->m_objectType) {
        default: {
            if (object->hasBeenActivatedByPlayer(pl->m_player1) &&
                (!pl->m_gameState.m_isDualMode ||
                 object->hasBeenActivatedByPlayer(pl->m_player2)))
                return;

            if (object == pl->m_player1CollisionBlock ||
                object == pl->m_player2CollisionBlock)
                return;

            if (object->m_objectType == GameObjectType::Modifier &&
                std::ranges::find(ALWAYS_ALLOWED_MODIFIERS,
                                  object->m_objectID) ==
                    ALWAYS_ALLOWED_MODIFIERS.end()) {
                if (!static_cast<EffectGameObject*>(object)->m_isTouchTriggered)
                    return;
            }

            if (!hb.interactable.enabled) break;

            // Preserve dirty flags to avoid side effects
            bool rectDirty      = object->m_isObjectRectDirty;
            bool offsetCalced   = object->m_boxOffsetCalculated;

            const ResolvedHBCategory* cat = &hb.interactable;

            if (geode::cast::typeinfo_cast<RingObject*>(object)) {
                if (pl->m_player1->m_touchingRings->containsObject(object) ||
                    pl->m_player2->m_touchingRings->containsObject(object))
                    cat = &hb.interactableActive;
            }

            auto objRect = object->getObjectRect();
            if (objRect.intersectsRect(pl->m_player1->getObjectRect()) ||
                objRect.intersectsRect(pl->m_player2->getObjectRect()))
                cat = &hb.interactableActive;

            if (auto ob = object->m_orientedBox)
                node->drawPolygon(ob->m_corners.data(), 4,
                                  cat->fill, width, cat->line);
            else
                node->drawRect(usingWidth(objRect, width),
                               cat->fill, width, cat->line);

            object->m_isObjectRectDirty     = rectDirty;
            object->m_boxOffsetCalculated   = offsetCalced;
            break;
        }

        case GameObjectType::Solid: {
            const auto& cat = object->m_isPassable ? hb.passable : hb.solid;
            if (cat.enabled)
                node->drawRect(usingWidth(object->getObjectRect(), width),
                               cat.fill, width, cat.line);
            break;
        }

        case GameObjectType::Slope: {
            const auto& cat = object->m_isPassable ? hb.passable : hb.solid;
            if (!cat.enabled) break;

            auto rect = object->getObjectRect();
            std::array<cocos2d::CCPoint, 3> verts = {
                rect.origin,
                cocos2d::CCPoint{rect.getMinX(), rect.getMaxY()},
                cocos2d::CCPoint{rect.getMaxX(), rect.getMinY()},
            };
            auto max = cocos2d::CCPoint{rect.getMaxX(), rect.getMaxY()};
            switch (object->m_slopeDirection) {
                case 0: case 7: verts[1] = max; break;
                case 1: case 5: verts[0] = max; break;
                case 3: case 6: verts[2] = max; break;
            }
            node->drawPolygon(verts.data(), 3, cat.fill, width, cat.line);
            break;
        }

        case GameObjectType::AnimatedHazard:
        case GameObjectType::Hazard: {
            if (!object->m_isActivated || object == pl->m_anticheatSpike ||
                !hb.hazard.enabled)
                return;

            bool rectDirty    = object->m_isObjectRectDirty;
            bool offsetCalced = object->m_boxOffsetCalculated;

            float radius = object->m_objectRadius *
                           std::max(object->m_scaleX, object->m_scaleY);
            if (radius > 0.0f) {
                node->drawCircle(object->getPosition(), radius,
                                 hb.hazard.fill, width, hb.hazard.line,
                                 circleSegments(radius));
            } else if (auto ob = object->m_orientedBox) {
                node->drawPolygon(ob->m_corners.data(), 4,
                                  hb.hazard.fill, width, hb.hazard.line);
            } else {
                node->drawRect(usingWidth(object->getObjectRect(), width),
                               hb.hazard.fill, width, hb.hazard.line);
            }

            object->m_isObjectRectDirty   = rectDirty;
            object->m_boxOffsetCalculated = offsetCalced;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Section iteration
// ---------------------------------------------------------------------------

static void iterateObjects(GJBaseGameLayer*                  pl,
                           std::function<void(GameObject*)>  callback) {
    auto& sections = pl->m_sections;
    for (int i = pl->m_leftSectionIndex;
         i <= pl->m_rightSectionIndex &&
         static_cast<size_t>(i) < sections.size(); ++i) {
        auto* hSection = sections[i];
        if (!hSection) continue;
        for (int j = pl->m_bottomSectionIndex;
             j <= pl->m_topSectionIndex &&
             static_cast<size_t>(j) < hSection->size(); ++j) {
            auto* section = hSection->at(j);
            if (!section) continue;
            const int count = pl->m_sectionSizes[i]->at(j);
            for (int k = 0; k < count; ++k) {
                auto* obj = section->at(k);
                if (obj) callback(obj);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Hitboxes::init
// ---------------------------------------------------------------------------

void Hitboxes::safeRelease(HitboxesDrawNode*& node) {
    if (node) {
        if (node->getParent()) node->removeFromParentAndCleanup(true);
        node->release();
        node = nullptr;
    }
}

void Hitboxes::init(GJBaseGameLayer* pl) {
    if (m_initialized) this->destroy();

    auto* parent   = pl->m_debugDrawNode->getParent();
    const int base = pl->m_uiLayer->getZOrder();

    auto makeNode = [&](int z, const char* id) -> HitboxesDrawNode* {
        auto* n = HitboxesDrawNode::create();
        n->retain();
        n->setID(id);
        n->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        parent->addChild(n, z);
        return n;
    };

    m_drawNode      = makeNode(base + 10000, "hitbox-node"_spr);
    m_trailDrawNode = makeNode(base + 9998,  "hitbox-trail-node"_spr);

    m_initialized = true;
}

void Hitboxes::draw(GJBaseGameLayer* pl) {
    if (!m_initialized) return;

    m_drawNode->clear();

    if (!m_enabled->inner()) {
        if (m_trailDirty) {
            m_trailDrawNode->clear();
            m_trailDirty = false;
        }
        return;
    }

    if (m_trailEnabled->inner() && m_trailDirty) {
        m_trailDrawNode->clear();
        m_trailDirty = false;

        auto&       hbs      = SLSettings::get()->hitboxes;
        auto&       settings = hbs;
        using       Type   = SLSettings::HitboxSettings::Type;
        const float width  = hbs.width / pl->m_gameState.m_cameraZoom;
        const float scale  = pl->m_objectLayer->getScale();
        const float objX   = pl->m_objectLayer->getPositionX();
        const float objY   = pl->m_objectLayer->getPositionY();

        const float minX = (-objX - pl->m_cameraWidth)  / scale;
        const float maxX = (-objX + pl->m_cameraWidth)  / scale;
        const float minY = (-objY - pl->m_cameraHeight) / scale;
        const float maxY = (-objY + pl->m_cameraHeight) / scale;

        const bool eRotated = HB_ENABLED(Type::PlayerRotated);
        const bool eSolid   = HB_ENABLED(Type::Player);
        const bool eCircle  = HB_ENABLED(Type::PlayerCircle);
        const bool eInner   = HB_ENABLED(Type::PlayerInner);

        if (eRotated || eSolid || eCircle || eInner) {
            auto resolve = [&](int t) -> ResolvedHBCategory {
                return resolveHB(hbs, t);
            };
            const auto rRot  = resolve(Type::PlayerRotated);
            const auto rSol  = resolve(Type::Player);
            const auto rCirc = resolve(Type::PlayerCircle);
            const auto rInner= resolve(Type::PlayerInner);

            auto drawTrail = [&](std::deque<HitboxTrailUnit>& trail) {
                bool hasLast = false;
                int  lastPx  = 0, lastPy = 0;

                for (const auto& unit : trail) {
                    if (!unit.shouldDraw(minX, maxX, minY, maxY)) continue;

                    const int px = static_cast<int>(
                        (unit.m_rect.getMidX() * scale + objX) * 2.0f);
                    const int py = static_cast<int>(
                        (unit.m_rect.getMidY() * scale + objY) * 2.0f);
                    if (hasLast && px == lastPx && py == lastPy) continue;
                    hasLast = true;
                    lastPx  = px;
                    lastPy  = py;

                    if (eSolid) {
                        auto r = usingWidth(unit.m_rect, width);
                        m_trailDrawNode->drawRect(r, rSol.fill, width, rSol.line);
                    }
                    if (eRotated && unit.m_rotated[0] != unit.m_rotated[2]) {
                        m_trailDrawNode->drawPolygon(
                            const_cast<cocos2d::CCPoint*>(unit.m_rotated.data()),
                            4, rRot.fill, width, rRot.line);
                    }
                    if (eInner) {
                        auto r = usingWidth(unit.m_scaled, width);
                        m_trailDrawNode->drawRect(r, rInner.fill, width, rInner.line);
                    }
                    if (eCircle) {
                        float radius = usingWidth(unit.m_rect, width).size.width * 0.5f;
                        if (radius > 0.0f) {
                            cocos2d::CCPoint center{unit.m_rect.getMidX(),
                                                    unit.m_rect.getMidY()};
                            m_trailDrawNode->drawCircle(
                                center, radius, rCirc.fill, width, rCirc.line,
                                trailCircleSegments(radius));
                        }
                    }
                }
            };

            drawTrail(m_trailP1);
            drawTrail(m_trailP2);
        }
    }

    {
        auto& hbs = SLSettings::get()->hitboxes;
        using Type = SLSettings::HitboxSettings::Type;
        const ResolvedObjectHB objHB{
            .width              = static_cast<float>(hbs.width / pl->m_gameState.m_cameraZoom),
            .interactable       = resolveHB(hbs, Type::Interactable),
            .interactableActive = resolveHB(hbs, Type::InteractableActive),
            .solid              = resolveHB(hbs, Type::Solid),
            .passable           = resolveHB(hbs, Type::Passable),
            .hazard             = resolveHB(hbs, Type::Hazard),
        };

        iterateObjects(pl, [&](GameObject* object) {
            drawObjectHitbox(m_drawNode, pl, object, objHB);
        });
    }

    auto& hbs = SLSettings::get()->hitboxes;
    drawPlayerHitbox(m_drawNode, pl, pl->m_player1, hbs);
    if (pl->m_gameState.m_isDualMode)
        drawPlayerHitbox(m_drawNode, pl, pl->m_player2, hbs);
}

static void appendTrailUnit(std::deque<HitboxTrailUnit>& trail,
                            PlayerObject*                 player,
                            int                           maxLength) {
    auto* box = player->getOrientedBox();
    trail.push_back({
        .m_rect    = player->getObjectRect(),
        .m_scaled  = player->getObjectRect(0.3f, 0.3f),
        .m_rotated = box ? box->m_corners
                         : std::array<cocos2d::CCPoint, 4>{},
    });
    while (static_cast<int>(trail.size()) > maxLength)
        trail.pop_front();
}

void Hitboxes::saveToTrail(GJBaseGameLayer* pl) {
    if (pl->m_levelEndAnimationStarted) return;
    if (!m_enabled->inner() || !m_trailEnabled->inner()) return;

    auto& hbs      = SLSettings::get()->hitboxes;
    int   maxLen   = std::clamp(hbs.trailMaxLength, 50, 8000);
    int   interval = std::max(1, hbs.trailRebuildInterval);

    appendTrailUnit(m_trailP1, pl->m_player1, maxLen);
    appendTrailUnit(m_trailP2, pl->m_player2, maxLen);

    ++m_trailStepCounter;
    if (m_trailStepCounter >= interval) {
        m_trailStepCounter = 0;
        m_trailDirty       = true;
    }
}

void Hitboxes::clearTrail() {
    m_trailP1.clear();
    m_trailP2.clear();
    m_trailStepCounter = 0;
    m_trailDirty = true;
}

void Hitboxes::destroy() {
    if (!m_initialized) return;

    safeRelease(m_drawNode);
    safeRelease(m_trailDrawNode);

    m_trailP1.clear();
    m_trailP2.clear();
    m_trailStepCounter = 0;
    m_trailDirty = false;
    m_initialized = false;
}
