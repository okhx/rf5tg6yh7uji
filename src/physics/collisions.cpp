#include "collisions.hpp"

#include <Geode/Enums.hpp>
#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "gravity.hpp"
#include "player.hpp"
#include "trajectory/trajectory.hpp"

namespace phys {
void activateForTrajectory(EffectGameObject* obj, PlayerObject* player) {
    auto bot = GrapeEngine::get();

    bot->trajectory().rememberActivatedObject(obj, player);
}

void bumpPlayerFromGJBGL(GJBaseGameLayer* pl, PlayerObject* player,
                         EffectGameObject* object) {
    if (pl->canBeActivatedByPlayer(player, object)) {
        cocos2d::CCPoint objPos = object->getPosition();

        player->m_lastPortalPos = objPos;
        activateForTrajectory(object, player);
        float force = 1.0;

        if (object->m_objectType == GameObjectType::PinkJumpPad) {
            if (player->m_isShip) {
                force = 0.35;
            } else if (player->m_isBird) {
                force = 0.4;
            } else if (player->m_isBall) {
                force = 0.7;
            } else if (player->m_isSpider) {
                force = 0.7;
            } else {
                force = 0.65;
            }
        } else if (object->m_objectType == GameObjectType::RedJumpPad) {
            if (player->m_isShip) {
                if (player->m_vehicleSize >= 1.0) {
                    force = 0.63;
                } else {
                    force = 0.95;
                }
            } else if (player->m_isBird) {
                if (player->m_vehicleSize >= 1.0) {
                    force = 0.6;
                } else {
                    force = 0.98;
                }
            } else {
                force = 1.25;
            }
        }
        player->m_lastActivatedPortal = object;
        phys::bumpPlayer(player, force, static_cast<int>(object->m_objectType),
                         false, object);
    }
}

void collisionCheckObjects(GJBaseGameLayer* pl, PlayerObject* player,
                           gd::vector<GameObject*>* objects, int objectCount,
                           float dt) {
    if (objectCount <= 0) return;

    CCRect playerRect = player->getObjectRect();

    [[maybe_unused]] float playerMinX = playerRect.getMinX();
    [[maybe_unused]] float playerMaxX = playerRect.getMaxX();
    [[maybe_unused]] float playerMinY = playerRect.getMinY();
    [[maybe_unused]] float playerMaxY = playerRect.getMaxY();

    for (int i = 0; i < objectCount; i++) {
        GameObject* object = objects->at(i);

        if (!object) continue;

        if (object->m_objectType == GameObjectType::Decoration ||
            object->m_objectType == GameObjectType::CollisionObject ||
            object->m_objectType == GameObjectType::SecretCoin ||
            object->m_objectType == GameObjectType::UserCoin ||
            object->m_objectType == GameObjectType::Collectible ||
            object->m_objectType == GameObjectType::EnterEffectObject ||
            object->m_objectID == 286 || object->m_objectID == 287 ||
            object->m_isGroupDisabled || object->m_isDisabled)
            continue;

        if (object->m_objectType == GameObjectType::Solid ||
            object->m_objectType == GameObjectType::Breakable) {
            if (pl->m_solidCollisionObjectsCount <
                pl->m_solidCollisionObjectsIndex) {
                pl->m_solidCollisionObjects.at(
                    pl->m_solidCollisionObjectsCount) = object;
                pl->m_solidCollisionObjectsCount++;
            } else {
                pl->m_solidCollisionObjects.push_back(object);
                pl->m_solidCollisionObjectsCount++;
                pl->m_solidCollisionObjectsIndex++;
            }

            continue;
        }

        if (object == pl->m_anticheatSpike) continue;

        if (object->m_objectType == GameObjectType::Hazard ||
            object->m_objectType == GameObjectType::AnimatedHazard) {
            if (pl->m_hazardCollisionObjectsCount <
                pl->m_hazardCollisionObjectsIndex) {
                pl->m_hazardCollisionObjects.at(
                    pl->m_hazardCollisionObjectsCount) = object;
                pl->m_hazardCollisionObjectsCount++;
            } else {
                pl->m_hazardCollisionObjects.push_back(object);
                pl->m_hazardCollisionObjectsCount++;
                pl->m_hazardCollisionObjectsIndex++;
            }

            continue;
        }

        auto bot = GrapeEngine::get();
        EffectGameObject* obj = (EffectGameObject*)object;
        if (!obj) continue;

        if (object->m_objectType != GameObjectType::Slope &&
            (bot->trajectory().playerHasActivated(player, obj) ||
             bot->trajectory().realPlayerHasActivated(player, obj)))
            continue;

        cocos2d::CCRect rect;
        if (object->m_objectType == GameObjectType::Slope) {
            rect = object->getObjectRect(2.0, 2.0);
        } else {
            rect = object->getObjectRect();
        }

        if (object->m_objectRadius <= 0.0) {
            if (!playerRect.intersectsRect(rect)) continue;
        } else if (!pl->playerCircleCollision(player, object)) {
            continue;
        }

        bool overlaps = true;

        if (object->m_shouldUseOuterOb &&
            (!pl->m_levelSettings->m_fixRadiusCollision ||
             object->m_objectRadius <= 0.0)) {
            OBB2D* box = object->getOrientedBox();
            player->updateOrientedBox();
            OBB2D* playerBox = ((GameObject*)(player))->getOrientedBox();
            overlaps = box->overlaps1Way(playerBox);
        }

        if (object->m_objectType == GameObjectType::Slope) {
            rect = object->getObjectRect();
        }

        if (!overlaps) continue;

        switch (object->m_objectType) {
            case GameObjectType::InverseGravityPortal:
                player->m_lastPortalPos = object->getPosition();
                player->m_lastActivatedPortal = object;
                activateForTrajectory(obj, player);
                phys::flipGravity(player, true);
                playerMinX = player->getObjectRect().getMinX();
                playerMaxX = player->getObjectRect().getMaxX();
                playerMinY = player->getObjectRect().getMinY();
                playerMaxY = player->getObjectRect().getMaxY();
                break;
            case GameObjectType::NormalGravityPortal:
                player->m_lastPortalPos = object->getPosition();
                player->m_lastActivatedPortal = object;
                activateForTrajectory(obj, player);
                phys::flipGravity(player, false);
                playerMinX = player->getObjectRect().getMinX();
                playerMaxX = player->getObjectRect().getMaxX();
                playerMinY = player->getObjectRect().getMinY();
                playerMaxY = player->getObjectRect().getMaxY();
                break;
            case GameObjectType::GravityTogglePortal:
                player->m_lastPortalPos = object->getPosition();
                player->m_lastActivatedPortal = object;
                activateForTrajectory(obj, player);
                phys::flipGravity(player, !player->m_isUpsideDown);
                break;
            case GameObjectType::TeleportPortal:
                if (pl->canBeActivatedByPlayer(player,
                                               (EffectGameObject*)object)) {
                    phys::teleportPlayer(pl, (TeleportPortalObject*)object,
                                         player);
                    activateForTrajectory(obj, player);
                }
                break;
            case GameObjectType::Slope:
                if (!player->m_isSideways) {
                    player->collidedWithSlopeInternal(dt, object, false);
                } else {
                    cocos2d::CCRect emptyRect =
                        cocos2d::CCRect{0.0, 0.0, 0.0, 0.0};

                    player->handleRotatedCollisionInternal(
                        dt, object, emptyRect, false, false, true);
                }

                playerMinX = player->getObjectRect().getMinX();
                playerMaxX = player->getObjectRect().getMaxX();
                playerMinY = player->getObjectRect().getMinY();
                playerMaxY = player->getObjectRect().getMaxY();

                break;
            case GameObjectType::CustomRing:
            case GameObjectType::DashRing:
            case GameObjectType::DropRing:
            case GameObjectType::GravityDashRing:
            case GameObjectType::GravityRing:
            case GameObjectType::GreenRing:
            case GameObjectType::PinkJumpRing:
            case GameObjectType::RedJumpRing:
            case GameObjectType::SpiderOrb:
            case GameObjectType::YellowJumpRing:
            case GameObjectType::TeleportOrb:
                if (!player->m_touchingRings->containsObject(object)) {
                    player->m_touchingRings->addObject(object);
                }
                player->m_touchedRings.insert(object->m_uniqueID);

                if (!player->m_isShip && !player->m_isBird &&
                    !player->m_isDart && !player->m_isSwing &&
                    !((RingObject*)object)->m_isSpawnOnly) {
                    phys::ringJump(player, (RingObject*)object);
                    activateForTrajectory(obj, player);
                }
                break;
            case GameObjectType::YellowJumpPad:
            case GameObjectType::PinkJumpPad:
            case GameObjectType::RedJumpPad:
            case GameObjectType::SpiderPad:
                phys::bumpPlayerFromGJBGL(pl, player, obj);
                break;
            case GameObjectType::GravityPad: {
                bool isFacingDown = false;
                if (player->m_isSideways) {
                    isFacingDown = object->isFacingLeft();
                } else {
                    isFacingDown = object->isFacingDown();
                }

                bool canBeActivated = pl->canBeActivatedByPlayer(player, obj);
                if (player->m_isUpsideDown == isFacingDown && canBeActivated) {
                    if (obj->m_isReverse) {
                        player->reversePlayer(obj);
                    }

                    player->m_lastPortalPos = obj->getPosition();
                    player->m_lastActivatedPortal = obj;
                    activateForTrajectory(obj, player);

                    phys::propellPlayer(player, 0.8, false, 10);
                    phys::flipGravity(player, !isFacingDown);
                    player->m_padRingRelated = true;
                }
                break;
            }
            case GameObjectType::MiniSizePortal:
                if (pl->canBeActivatedByPlayer(player, obj)) {
                    player->m_lastPortalPos = obj->getPosition();
                    player->m_lastActivatedPortal = obj;
                    activateForTrajectory(obj, player);

                    phys::togglePlayerScale(player, true);
                }
                break;
            case GameObjectType::RegularSizePortal:
                if (pl->canBeActivatedByPlayer(player, obj)) {
                    player->m_lastPortalPos = obj->getPosition();
                    player->m_lastActivatedPortal = obj;
                    activateForTrajectory(obj, player);

                    phys::togglePlayerScale(player, false);
                }
                break;
            case GameObjectType::Special:
                if (object->m_objectID == 0x743) {
                    player->m_stateHitHead = 2;
                } else if (object->m_objectID == 0x6db) {
                    player->m_stateDartSlide = 2;
                } else if (object->m_objectID == 0x715) {
                    player->m_stateNoAutoJump = 2;
                } else if (object->m_objectID == 0x725 && player->m_isDashing) {
                    phys::stopDashing(player);
                    player->m_jumpBuffered = false;
                } else if (object->m_objectID == 0xb32) {
                    player->m_stateFlipGravity = 2;
                } else if (object->m_objectID == 2069 ||
                           object->m_objectID == 3645) {
                    player->m_stateForce = 2;
                    ForceBlockGameObject* forceBlock =
                        (ForceBlockGameObject*)object;
                    int forceID = forceBlock->m_forceID;
                    if (forceID > 0) {
                        if (player->m_jumpPadRelated.contains(forceID)) {
                            if (player->m_jumpPadRelated.at(forceID)) {
                                break;
                            }
                        }
                        player->m_jumpPadRelated.insert({forceID, true});
                    }

                    CCPoint force = forceBlock->calculateForceToTarget(player);
                    player->m_stateForceVector += force;
                }
                break;
            case GameObjectType::CubePortal:
            case GameObjectType::ShipPortal:
            case GameObjectType::BallPortal:
            case GameObjectType::UfoPortal:
            case GameObjectType::WavePortal:
            case GameObjectType::SpiderPortal:
            case GameObjectType::SwingPortal:
            case GameObjectType::RobotPortal:
                activatingPortal(pl, player, obj);
                break;
            case GameObjectType::EnterEffectObject:
            case GameObjectType::Modifier:
                obj->activatedByPlayer(player);

                if (obj->m_isTouchTriggered) {
                    phys::triggerObject(obj, pl, player);
                }
                break;
            default:
                break;
        }
    }
}

void triggerObject(EffectGameObject* object, GJBaseGameLayer* pl,
                   PlayerObject* player) {
    auto bot = GrapeEngine::get();
    switch (object->m_objectID) {
        case 200:
            *(float*)(&pl->m_gameState.m_timeModRelated) = 0.7;
            break;
        case 201:
            *(float*)(&pl->m_gameState.m_timeModRelated) = 0.9;
            break;
        case 202:
            *(float*)(&pl->m_gameState.m_timeModRelated) = 1.1;
            break;
        case 203:
            *(float*)(&pl->m_gameState.m_timeModRelated) = 1.3;
            break;
        case 1334:
            *(float*)(&pl->m_gameState.m_timeModRelated) = 1.6;
            break;
        case 2066: {
            if (object->m_followCPP) {
                break;
            }

            bool isP2 =
                bot->trajectory().unsafeInner()->m_fakePlayer2 == player;
            if (!object->m_targetPlayer2 && !isP2) {
                player->m_gravityMod = object->m_gravityValue;
            }
            if (object->m_targetPlayer2 && isP2) {
                player->m_gravityMod = object->m_gravityValue;
            }
            break;
        }
        case 2900: {
            RotateGameplayGameObject* rotateObj =
                (RotateGameplayGameObject*)object;

            if (rotateObj->m_changeChannel) {
                pl->m_gameState.m_currentChannel = rotateObj->m_targetChannelID;
                pl->m_gameState
                    .m_spawnChannelRelated1[rotateObj->m_targetChannelID] =
                    rotateObj->m_groundDirection == 2 ||
                    rotateObj->m_groundDirection == 3;
            }

            if (rotateObj->m_channelOnly) break;

            auto rotate = [rotateObj](PlayerObject* p) {
                p->rotateGameplay(
                    rotateObj->m_moveDirection, rotateObj->m_groundDirection,
                    rotateObj->m_editVelocity, rotateObj->m_velocityModX,
                    rotateObj->m_velocityModY, rotateObj->m_overrideVelocity,
                    rotateObj->m_dontSlide);
            };

            rotate(player);

            if (pl->m_gameState.m_isDualMode &&
                bot->trajectory().isFakePlayer(player)) {
                PlayerObject* other = bot->trajectory().getOtherPlayer(player);
                if (other && other != player &&
                    !bot->trajectory().playerHasActivated(other, object) &&
                    !bot->trajectory().realPlayerHasActivated(other, object)) {
                    rotate(other);
                    phys::activateForTrajectory(object, other);
                }
            }

            break;
        }
        case 3022: {
            phys::teleportPlayer(pl, (TeleportPortalObject*)object, player);
            break;
        }
        default: {
            break;
        }
    }

    phys::activateForTrajectory(object, player);
}

void checkSpawnObjects(GJBaseGameLayer* pl, PlayerObject* player) {
    CCPoint position = player->getPosition();

    cocos2d::CCArray* objects =
        (cocos2d::CCArray*)pl->m_spawnObjects->objectForKey(
            pl->m_gameState.m_currentChannel);
    if (!objects) {
        return;
    }

    const int channel = pl->m_gameState.m_currentChannel;
    const auto& related0 = pl->m_gameState.m_spawnChannelRelated0;
    const auto& related1 = pl->m_gameState.m_spawnChannelRelated1;

    auto startIt = related0.find(channel);
    auto backIt = related1.find(channel);

    int startingIndex = startIt == related0.end() ? 0 : startIt->second;
    bool goingBack = backIt == related1.end() ? false : backIt->second;

    for (int i = startingIndex; static_cast<unsigned int>(i) < objects->count();
         i++) {
        SpawnTriggerGameObject* object =
            (SpawnTriggerGameObject*)objects->objectAtIndex(i);
        if (object->m_objectID != 2066 && object->m_objectID != 2900 &&
            object->m_objectID != 3022 && object->m_objectID != 901) {
            continue;
        }

        CCPoint objectPos = object->m_speedStart;

        if (player->m_isSideways) {
            if (goingBack) {
                if (objectPos.y < position.y) break;
            } else {
                if (objectPos.y > position.y) break;
            }
        } else {
            if (goingBack) {
                if (objectPos.x < position.x) break;
            } else {
                if (objectPos.x > position.x) break;
            }
        }

        if (object->m_isGroupDisabled) continue;
        auto bot = GrapeEngine::get();
        if (bot->trajectory().playerHasActivated(player, object) ||
            bot->trajectory().realPlayerHasActivated(player, object))
            continue;

        if (!object->m_isTouchTriggered) {
            phys::triggerObject(object, pl, player);
        }
    }
}
}  // namespace phys
