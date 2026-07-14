#include "trajectory.hpp"

#include <array>

#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "ccTypes.h"
#include "checkpoint/checkpoint.hpp"
#include "physics/collisions.hpp"
#include "physics/object.hpp"
#include "replay/system.hpp"
#include "settings/settings.hpp"

#ifdef SILICATE_PROTECT
#include "VMProtect/VMProtectSDK.h"
#endif

using namespace geode::prelude;

static cocos2d::ccColor4F toCocosColor(const float colors[4]) {
    return {colors[0], colors[1], colors[2], colors[3]};
}

static cocos2d::ccColor4F toCocosColorNegative(const float colors[4]) {
    return {1.0f - colors[0], 1.0f - colors[1], 1.0f - colors[2], colors[3]};
}

static cocos2d::CCRect usingWidth(const cocos2d::CCRect& r, float w) {
    return {r.origin.x + w, r.origin.y + w,
            r.size.width - w * 2.0f, r.size.height - w * 2.0f};
}

static void drawRect(CCDrawNode* node, CCRect& rect, ccColor4F color, float w) {
    node->drawRect(rect, {0, 0, 0, 0}, w, color);
}

static void drawRotatedRect(CCDrawNode* node, CCRect& rect, float angle,
                             ccColor4F color, float w) {
    std::array<CCPoint, 4> verts = {
        CCPoint{rect.getMinX(), rect.getMinY()},
        CCPoint{rect.getMaxX(), rect.getMinY()},
        CCPoint{rect.getMaxX(), rect.getMaxY()},
        CCPoint{rect.getMinX(), rect.getMaxY()},
    };
    const CCPoint center{rect.getMidX(), rect.getMidY()};
    const float rad = -CC_DEGREES_TO_RADIANS(angle);
    for (auto& v : verts) v = v.rotateByAngle(center, rad);
    node->drawPolygon(verts.data(), verts.size(), {0, 0, 0, 0}, w, color);
}

static uint64_t packPlayerFlags(PlayerObject* p) {
    uint64_t flags = 0;
    int bit = 0;
    auto set = [&](bool v) { if (v) flags |= (uint64_t(1) << bit); ++bit; };
    set(p->m_isShip);   set(p->m_isBird);   set(p->m_isDart);
    set(p->m_isSwing);  set(p->m_isBall);   set(p->m_isSpider);
    set(p->m_isRobot);  set(p->m_isUpsideDown); set(p->m_isDead);
    set(p->m_isOnGround2); set(p->m_isSideways); set(p->m_isDashing);
    set(p->m_isAccelerating); set(p->m_holdingLeft); set(p->m_holdingRight);
    set(p->m_jumpBuffered); set(p->m_isPlatformer); set(p->m_isGoingLeft);
    set(p->m_maybeIsBoosted);
    return flags;
}

static size_t hashTrajectoryCategories() {
    size_t h = 0;
    for (const auto& [key, state] : SLSettings::get()->trajectory.categories) {
        size_t e = std::hash<int>{}(key);
        e = e * 31 + (state.enabled ? 1u : 0u);
        for (const float c : state.colors) e = e * 31 + std::hash<float>{}(c);
        h ^= e + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
}

Trajectory::Signature Trajectory::computeSignature(GJBaseGameLayer* pl) {
    Signature s;
    s.frame = Bot::get()->updater().getFrame();

    auto fillPlayer = [](float out[7], uint64_t& flags, PlayerObject* p) {
        const CCPoint pos = p->getPosition();
        out[0] = pos.x;            out[1] = pos.y;
        out[2] = p->getRotation(); out[3] = p->m_yVelocity;
        out[4] = p->m_platformerXVelocity;
        out[5] = p->m_gravityMod;  out[6] = p->m_vehicleSize;
        flags  = packPlayerFlags(p);
    };

    if (pl->m_player1) fillPlayer(s.p1, s.p1flags, pl->m_player1);
    if (pl->m_player2) fillPlayer(s.p2, s.p2flags, pl->m_player2);

    s.timeWarp    = pl->m_gameState.m_timeWarp;
    s.cameraZoom  = pl->m_gameState.m_cameraZoom;
    s.width       = static_cast<float>(m_state->m_width->inner());
    s.length      = static_cast<float>(m_state->m_length->inner());
    s.maxSteps    = SLSettings::get()->trajectory.maxSteps;

    uint32_t b = 0;
    int bit = 0;
    auto setb = [&](bool v) { if (v) b |= (uint32_t(1) << bit); ++bit; };
    setb(m_state->m_enabled->inner());
    setb(pl->m_gameState.m_isDualMode);
    setb(pl->m_isPlatformer);
    setb(pl->m_levelSettings->m_twoPlayerMode);
    setb(m_p1Holding);
    setb(m_p2Holding);
    setb(pl->m_player1 != nullptr);
    setb(pl->m_player2 != nullptr);
    s.boolPack = b;

    s.categoriesHash = hashTrajectoryCategories();
    return s;
}

int Trajectory::getPredictionLength() {
    auto bot = Bot::get();
    auto pl  = GJBaseGameLayer::get();
    if (!pl) return 0;

    auto& ts = SLSettings::get()->trajectory;
    int steps = static_cast<int>(
        ts.length * std::max(bot->updater().getTps(), 240.0) /
        pl->m_gameState.m_timeWarp);

    if (ts.maxSteps > 0)
        steps = std::min(steps, ts.maxSteps);

    return steps;
}

bool Trajectory::iterate(GJBaseGameLayer* pl, PlayerObject* player, int mode,
                         float* colors, bool& /*hasHeld*/, int& stepCount,
                         PredictionConfig config) {
    const CCPoint prevPos = player->getPosition();

    auto& updater = Bot::get()->updater();
    const float physicsDt = updater.getPhysicsDt();
    const float timeWarp  = updater.getTimeWarp();

    pl->m_gameState.m_totalTime    += physicsDt;
    pl->m_gameState.m_unkDouble3   += physicsDt / timeWarp;
    pl->m_gameState.m_currentProgress++;
    pl->m_gameState.m_unkUint5     += static_cast<int>(roundf(timeWarp * 1000.0f));

    player->m_totalTime += physicsDt;

    float playerSpeed = *reinterpret_cast<float*>(&pl->m_gameState.m_timeModRelated);
    if (playerSpeed != 0.0f) {
        pl->m_gameState.m_timeModRelated  = 0;
        pl->m_gameState.m_timeModRelated2 = 0;
        player->updateTimeMod(playerSpeed, true);
    }

    player->m_collisionLogTop->removeAllObjects();
    player->m_collisionLogBottom->removeAllObjects();
    player->m_collisionLogLeft->removeAllObjects();
    player->m_collisionLogRight->removeAllObjects();

    if ((m_deadP1 && (mode & TrajectoryMode::Player1) != 0) ||
        (m_deadP2 && (mode & TrajectoryMode::Player2) != 0)) {
        if (stepCount > 1) drawHitbox(player);
        return true;
    }

    if (!m_actions.empty()) {
        for (auto& action : m_actions) {
            if (action.m_delay == 0) {
                action.m_func();
                action.m_executed = true;
            } else {
                --action.m_delay;
            }
        }
        std::erase_if(m_actions, [](const auto& a) { return a.m_executed; });
    }

    const float delta = (config.m_overridenTPS == 0.0)
                            ? m_delta
                            : static_cast<float>((1.0 / config.m_overridenTPS) * 60.0);

    player->m_playEffects = false;
    player->update(delta);
    player->m_unkUnused3  = player->getRotation();
    player->updateRotation(delta);
    player->m_shipRotation = player->getPosition();

    if (pl->checkCollisions(player, delta, false) == 1)
        hasDied(player);

    phys::checkSpawnObjects(pl, player);
    pl->m_effectManager->postCollisionCheck();

    const float width = m_state->m_width->inner() / pl->m_gameState.m_cameraZoom;
    m_node->drawSegment(
        prevPos, player->getPosition(), width,
        (player == m_fakePlayer1) ? toCocosColor(colors)
                                  : toCocosColorNegative(colors));

    ++stepCount;
    return false;
}

TrajectoryPlayerData Trajectory::runPrediction(GJBaseGameLayer* pl,
                                               PlayerObject* player,
                                               PlayerObject* other, int mode,
                                               float* colors, bool both,
                                               PredictionConfig config) {
    auto   bot       = Bot::get();
    bool   hasClickP1 = false;
    bool   hasClickP2 = false;

    int iterations = getPredictionLength();
    if (config.m_maxLength > 0)
        iterations = std::min(iterations, config.m_maxLength);

    const int playerMask = (player == m_fakePlayer1) ? TrajectoryMode::Player1
                                                      : TrajectoryMode::Player2;
    const bool dualBoth  = both && pl->m_gameState.m_isDualMode;
    const float width    = m_state->m_width->inner() / pl->m_gameState.m_cameraZoom;

    std::array<PlayerObject*, 2> activePlayers = {player, other};
    const int activeCount = dualBoth ? 2 : 1;

    if (bot->isRecording()) {
        for (int i = 0; i < activeCount; ++i) {
            PlayerObject* plr = activePlayers[i];
            switch (mode & CLICK_MASK) {
                case TrajectoryMode::Hold:
                    plr->pushButton(PlayerButton::Jump);
                    break;
                case TrajectoryMode::Swift:
                    plr->pushButton(PlayerButton::Jump);
                    plr->releaseButton(PlayerButton::Jump);
                    break;
                case TrajectoryMode::Release:
                    plr->releaseButton(PlayerButton::Jump);
                    plr->m_jumpBuffered = false;
                    break;
            }

            if (pl->m_levelSettings->m_platformerMode) {
                switch (mode & DIRECTION_MASK) {
                    case TrajectoryMode::Left:
                        plr->releaseButton(PlayerButton::Right);
                        plr->pushButton(PlayerButton::Left);
                        break;
                    case TrajectoryMode::Right:
                        plr->releaseButton(PlayerButton::Left);
                        plr->pushButton(PlayerButton::Right);
                        break;
                    default:
                        plr->releaseButton(PlayerButton::Left);
                        plr->releaseButton(PlayerButton::Right);
                        break;
                }
            }
        }
    }

    for (int i = 0; i < activeCount; ++i) {
        PlayerObject* plr = activePlayers[i];
        const CCPoint pos = plr->getPosition();
        m_node->drawSegment(pos, plr->getPosition(), width,
                            plr == m_fakePlayer1 ? toCocosColor(colors)
                                                 : toCocosColorNegative(colors));
    }

    bool breakP1 = false;
    bool breakP2 = false;
    int  stepP1  = 0;
    int  stepP2  = 0;

    int trajectoryInputIndex = bot->replaySystem().getInputIndex();
    const auto& inputs = bot->replaySystem().m_actionAtom.m_actions;

    int predCount = 0;
    for (int i = 0; i < iterations; ++i) {
        if (bot->isPlaying()) {
            const uint64_t frame = bot->updater().getFrame() + i;
            while (trajectoryInputIndex < static_cast<int>(inputs.size()) &&
                   inputs[trajectoryInputIndex].m_frame <= frame) {
                const auto& input = inputs[trajectoryInputIndex];
                PlayerObject* p   = input.m_player2 ? m_fakePlayer2 : m_fakePlayer1;
                PlayerObject* op  = input.m_player2 ? m_fakePlayer1 : m_fakePlayer2;

                if (input.m_holding) {
                    p->pushButton(static_cast<PlayerButton>(input.m_type));
                    if (dualBoth) op->pushButton(static_cast<PlayerButton>(input.m_type));
                } else {
                    p->releaseButton(static_cast<PlayerButton>(input.m_type));
                    if (dualBoth) op->releaseButton(static_cast<PlayerButton>(input.m_type));
                }
                ++trajectoryInputIndex;
            }
        }

        if (!breakP1) {
            breakP1 = iterate(pl, player, mode | playerMask, colors,
                              hasClickP1, stepP1, config);
            ++predCount;
        }
        if (dualBoth && !breakP2) {
            breakP2 = iterate(pl, other, mode | TrajectoryMode::Player2, colors,
                              hasClickP2, stepP2, config);
        }
    }

    return {
        .position    = player->getPosition(),
        .hitbox      = player->getObjectRect(),
        .innerHitbox = player->getObjectRect(0.3, 0.3),
        .rotation    = player->getRotationX(),
        .p1          = (mode & TrajectoryMode::Player1) != 0,
        .holding     = (mode & TrajectoryMode::Hold) != 0,
        .score       = predCount,
    };
}

TrajectoryPlayerData Trajectory::simulate(GJBaseGameLayer* pl, bool p1,
                                          int mode, bool clickBothPlayers,
                                          PredictionConfig config) {
    auto& ts = SLSettings::get()->trajectory;

    PlayerObject* player         = p1 ? m_fakePlayer1 : m_fakePlayer2;
    PlayerObject* realPlayer     = p1 ? pl->m_player1  : pl->m_player2;
    PlayerObject* otherPlayer    = p1 ? m_fakePlayer2  : m_fakePlayer1;
    PlayerObject* otherReal      = p1 ? pl->m_player2  : pl->m_player1;

    const bool isLeft        = (mode & TrajectoryMode::Left)  != 0;
    const bool isRight       = (mode & TrajectoryMode::Right) != 0;
    const bool isDirectionless = !(isLeft || isRight);
    const bool holdingDir    = (isLeft  && realPlayer->m_holdingLeft)  ||
                               (isRight && realPlayer->m_holdingRight) ||
                               (isDirectionless && !realPlayer->m_holdingLeft &&
                                                   !realPlayer->m_holdingRight);
    const bool holdingOpp    = (isLeft  && realPlayer->m_holdingRight) ||
                               (isRight && realPlayer->m_holdingLeft);

    const int platformerMask = pl->m_isPlatformer ? TrajectoryMode::Platformer : 0;

    if (!config.m_bypassConfig) {
        auto& cats = ts.categories;
        const bool mainEnabled   = cats[mode | platformerMask].enabled;
        const bool followEnabled = cats[(mode & CLICK_MASK) | TrajectoryMode::FollowPlayer |
                                        platformerMask].enabled && holdingDir;
        const bool oppEnabled    = cats[(mode & CLICK_MASK) | TrajectoryMode::FollowOpposite |
                                        platformerMask].enabled && holdingOpp;
        if (!(mainEnabled || followEnabled || oppEnabled)) return {};
    }

    const GJGameState savedState = pl->m_gameState;
    EffectManagerState ems;
    pl->m_effectManager->saveToState(ems);

    auto syncPlayer = [](PlayerObject* fake, PlayerObject* real) {
        fake->copyAttributes(real);
        fake->m_maybeReducedEffects = true;
        SavedPlayerCheckpoint cp = SavedPlayerCheckpoint::create(real);
        cp.apply(fake);
        fake->setPosition(real->m_position);
        fake->setRotation(real->getRotation());
    };

    syncPlayer(player, realPlayer);
    if (clickBothPlayers && otherReal)
        syncPlayer(otherPlayer, otherReal);

    m_deadP1 = false;
    m_deadP2 = false;

    float* colors = ts.categories[mode | platformerMask].colors.data();
    auto predicted = runPrediction(pl, player, otherPlayer, mode, colors,
                                   clickBothPlayers, config);

    player->setVisible(false);
    otherPlayer->setVisible(false);

    deactivateAllRemembered();

    pl->m_gameState = savedState;
    pl->m_effectManager->loadFromState(ems);

    return predicted;
}

void Trajectory::update(GJBaseGameLayer* pl) {
    if (!pl) return;

    m_fakePlayer1->setVisible(false);
    m_fakePlayer2->setVisible(false);

    if (auto lel = LevelEditorLayer::get();
        lel && lel->m_playbackMode == PlaybackMode::Not)
        return;

    if (!m_state->m_enabled->inner()) {
        m_node->setVisible(false);
        return;
    }

    m_node->setVisible(true);

    const Signature sig   = computeSignature(pl);
    const bool needsRebuild = !m_calculated || !(sig == m_lastSignature);

    if (!needsRebuild) goto applyLayoutColors;

    {
        m_drawing = true;
        m_node->clear();

        auto& bot  = *Bot::get();
        m_lastFrame = bot.updater().getFrame();
        m_delta     = bot.updater().getPhysicsDt() * 60.0f;

        m_fakePlayer1->setVisible(false);
        if (pl->m_player2) m_fakePlayer2->setVisible(false);

        const bool notTwoPlayer = !pl->m_levelSettings->m_twoPlayerMode;

        if (pl->m_player1) {
            simulate(pl, true, TrajectoryMode::Hold,    notTwoPlayer);
            simulate(pl, true, TrajectoryMode::Swift,   notTwoPlayer);
            simulate(pl, true, TrajectoryMode::Release, notTwoPlayer);

            if (pl->m_isPlatformer) {
                for (int dir : {0, (int)TrajectoryMode::Left, (int)TrajectoryMode::Right}) {
                    simulate(pl, true, TrajectoryMode::Hold    | dir, notTwoPlayer);
                    simulate(pl, true, TrajectoryMode::Swift   | dir, notTwoPlayer);
                    simulate(pl, true, TrajectoryMode::Release | dir, notTwoPlayer);
                }
            }

            m_fakePlayer1->setVisible(false);
            if (pl->m_player2) m_fakePlayer2->setVisible(false);
        }

        if (pl->m_player2 && pl->m_gameState.m_isDualMode &&
            pl->m_levelSettings->m_twoPlayerMode) {
            simulate(pl, false, TrajectoryMode::Hold,    false);
            simulate(pl, false, TrajectoryMode::Swift,   false);
            simulate(pl, false, TrajectoryMode::Release, false);

            if (pl->m_isPlatformer) {
                for (int dir : {0, (int)TrajectoryMode::Left, (int)TrajectoryMode::Right}) {
                    simulate(pl, false, TrajectoryMode::Hold    | dir, false);
                    simulate(pl, false, TrajectoryMode::Swift   | dir, false);
                    simulate(pl, false, TrajectoryMode::Release | dir, false);
                }
            }
        }

        m_calculated    = true;
        m_lastSignature = sig;
        m_drawing       = false;
    }

applyLayoutColors:
    auto& updater = Bot::get()->updater();
    if (!updater.m_layoutMode->inner() || LevelEditorLayer::get()) return;

    constexpr std::array<int, 5> colorIds = {1000, 1001, 1009, 1013, 1014};
    for (int colorId : colorIds) {
        ColorAction* action = pl->m_effectManager->getColorAction(colorId);
        if (!action) continue;

        switch (colorId) {
            case 1000: {
                auto& col = SLSettings::get()->layoutBgColor;
                action->m_color = {static_cast<GLubyte>(col[0] * 255),
                                   static_cast<GLubyte>(col[1] * 255),
                                   static_cast<GLubyte>(col[2] * 255)};
                break;
            }
            case 1001:
            case 1009: {
                auto& col = SLSettings::get()->layoutGroundColor;
                action->m_color = {static_cast<GLubyte>(col[0] * 255),
                                   static_cast<GLubyte>(col[1] * 255),
                                   static_cast<GLubyte>(col[2] * 255)};
                break;
            }
            default:
                action->m_color = {40, 125, 255};
                break;
        }
    }
}

void Trajectory::drawHitbox(PlayerObject* player) {
#define CC_COLOR(color_type) \
    *reinterpret_cast<cocos2d::ccColor4F*>(settings.categories[color_type].colors.data())

    const float width = m_state->m_width->inner() /
                        GJBaseGameLayer::get()->m_gameState.m_cameraZoom;
    CCRect rect   = usingWidth(player->getObjectRect(), width);
    CCRect scaled = usingWidth(player->getObjectRect(0.3f, 0.3f), width);

    auto& settings = SLSettings::get()->hitboxes;
    using Type = SLSettings::HitboxSettings::Type;

    drawRotatedRect(m_node, rect, player->getRotation(), CC_COLOR(Type::PlayerRotated), width);
    drawRect(m_node, rect,   CC_COLOR(Type::Player),      width);
    drawRect(m_node, scaled, CC_COLOR(Type::PlayerInner), width);
#undef CC_COLOR
}

bool Trajectory::playerHasActivated(PlayerObject* player,
                                    EnhancedGameObject* object) {
    if (!object || object->m_isMultiActivate) return false;
    if (player == m_fakePlayer1)
        return m_activatedObjectsP1.contains(reinterpret_cast<uintptr_t>(object));
    if (player == m_fakePlayer2)
        return m_activatedObjectsP2.contains(reinterpret_cast<uintptr_t>(object));
    return false;
}

bool Trajectory::realPlayerHasActivated(PlayerObject* player,
                                        EnhancedGameObject* object) {
    if (!object) return false;
    if (!isFakePlayer(player))
        return phys::hasBeenActivatedByPlayer(player, object);

    auto pl = GJBaseGameLayer::get();
    PlayerObject* realPlayer = (player == m_fakePlayer1) ? pl->m_player1 : pl->m_player2;
    return phys::hasBeenActivatedByPlayer(realPlayer, object);
}

static void* RingObject_spawnCircle_orig = nullptr;

void RingObject_spawnCircle(RingObject* self) {
    auto func = reinterpret_cast<void (*)(RingObject*)>(RingObject_spawnCircle_orig);
    if (!Bot::get()->trajectory().drawing()) func(self);
}
