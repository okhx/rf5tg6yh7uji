#include "pathfinder.hpp"

#include "checkpoint/fix.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "trajectory/trajectory.hpp"

#include <cmath>
#include <limits>

namespace {

constexpr int timingFrames(int cycle, float percent) {
    return std::max(1, static_cast<int>(cycle * percent * .01f + .5f));
}

static_assert(timingFrames(10, 40.0f) == 4);

constexpr bool shouldEscape(int currentScore, int alternativeScore,
                            int reactionFrames) {
    return currentScore <= reactionFrames &&
           alternativeScore > currentScore;
}

static_assert(shouldEscape(8, 20, 12));
static_assert(!shouldEscape(13, 20, 12));
static_assert(!shouldEscape(8, 8, 12));

constexpr bool shouldJump(float hazard, float block, float hazardWindow,
                          float blockWindow) {
    return hazard <= hazardWindow || block <= blockWindow;
}

static_assert(shouldJump(7.0f, 100.0f, 8.0f, 60.0f));
static_assert(shouldJump(100.0f, 50.0f, 8.0f, 60.0f));
static_assert(!shouldJump(9.0f, 61.0f, 8.0f, 60.0f));

// Walk the level's loaded section grid and hand every live nearby object to the
// callback -- this is how we "see all the hitboxes" around the player. Mirrors
// the scan the moving-gap assist already uses.
template <class Callback>
void visitNearbyObjects(PlayLayer* layer, Callback&& callback) {
    const int left = std::max(0, layer->m_leftSectionIndex);
    const int right = std::min(
        layer->m_rightSectionIndex,
        static_cast<int>(layer->m_sections.size()) - 1);
    for (int x = left; x <= right; ++x) {
        auto* column = layer->m_sections[x];
        if (!column || static_cast<size_t>(x) >= layer->m_sectionSizes.size() ||
            !layer->m_sectionSizes[x])
            continue;
        const int bottom = std::max(0, layer->m_bottomSectionIndex);
        const int top = std::min({
            layer->m_topSectionIndex,
            static_cast<int>(column->size()) - 1,
            static_cast<int>(layer->m_sectionSizes[x]->size()) - 1});
        for (int y = bottom; y <= top; ++y) {
            auto* section = column->at(y);
            if (!section) continue;
            const int count = std::min(
                layer->m_sectionSizes[x]->at(y),
                static_cast<int>(section->size()));
            for (int i = 0; i < count; ++i) {
                auto* object = section->at(i);
                if (object && !object->m_isDisabled &&
                    !object->m_isGroupDisabled)
                    callback(object);
            }
        }
    }
}

bool isRing(GameObjectType type) {
    return type == GameObjectType::CustomRing ||
           type == GameObjectType::DashRing ||
           type == GameObjectType::DropRing ||
           type == GameObjectType::GravityDashRing ||
           type == GameObjectType::GravityRing ||
           type == GameObjectType::GreenRing ||
           type == GameObjectType::PinkJumpRing ||
           type == GameObjectType::RedJumpRing ||
           type == GameObjectType::SpiderOrb ||
           type == GameObjectType::YellowJumpRing ||
           type == GameObjectType::TeleportOrb;
}

struct NearbyObjects {
    float hazard = std::numeric_limits<float>::max();
    float block = std::numeric_limits<float>::max();
    float ring = std::numeric_limits<float>::max();
    float lowerClearance = std::numeric_limits<float>::max();
    float upperClearance = std::numeric_limits<float>::max();
};

bool holdingJump(PlayerObject* player) {
    const auto held = player->m_holdingButtons.find(
        static_cast<int>(PlayerButton::Jump));
    return held != player->m_holdingButtons.end() && held->second;
}

NearbyObjects inspectNearby(PlayLayer* pl, PlayerObject* player) {
    NearbyObjects nearby;
    const float dir = player->m_isGoingLeft ? -1.0f : 1.0f;
    const auto playerRect = player->getObjectRect();
    const float centerY = playerRect.getMidY();
    const float horizontalSpeed =
        FrameEngine::playerSpeedUnits(player->m_playerSpeed);
    const float emergencyAhead = horizontalSpeed * .06f + 12.0f;
    visitNearbyObjects(pl, [&](GameObject* object) {
        const auto type = object->m_objectType;
        const auto rect = object->getObjectRect();
        const float ahead = dir > 0 ? rect.getMinX() - playerRect.getMaxX()
                                    : playerRect.getMinX() - rect.getMaxX();
        if (ahead < -20.0f || ahead > 300.0f) return;

        if (isRing(type)) {
            if (rect.getMaxY() >= playerRect.getMinY() - 30.0f &&
                rect.getMinY() <= playerRect.getMaxY() + 30.0f)
                nearby.ring = std::min(nearby.ring, std::max(0.0f, ahead));
            return;
        }

        const bool hazard = type == GameObjectType::Hazard ||
                            type == GameObjectType::AnimatedHazard;
        const bool solid = type == GameObjectType::Solid ||
                           type == GameObjectType::Breakable;
        if (!hazard && !solid) return;

        const bool verticalThreat =
            hazard
                ? rect.getMaxY() >= playerRect.getMinY() - 8.0f &&
                      rect.getMinY() <= playerRect.getMaxY() + 8.0f
                : (!player->m_isUpsideDown
                       ? rect.getMaxY() > playerRect.getMinY() + 4.0f &&
                             rect.getMinY() <= playerRect.getMaxY() + 8.0f
                       : rect.getMinY() < playerRect.getMaxY() - 4.0f &&
                             rect.getMaxY() >= playerRect.getMinY() - 8.0f);
        if (verticalThreat) {
            auto& distance = hazard ? nearby.hazard : nearby.block;
            distance = std::min(distance, std::max(0.0f, ahead));
        }

        if (ahead <= emergencyAhead) {
            if (rect.getMaxY() <= centerY)
                nearby.lowerClearance = std::min(
                    nearby.lowerClearance,
                    std::max(0.0f, playerRect.getMinY() - rect.getMaxY()));
            if (rect.getMinY() >= centerY)
                nearby.upperClearance = std::min(
                    nearby.upperClearance,
                    std::max(0.0f, rect.getMinY() - playerRect.getMaxY()));
        }
    });
    return nearby;
}

}  // namespace

void Pathfinder::decide(PlayLayer* pl, Trajectory* traj, bool p1,
                        bool& clicked) {
    using Mode = Trajectory::TrajectoryMode;
    PlayerObject* player = p1 ? pl->m_player1 : pl->m_player2;
    if (!player || player->m_isDead) return;

    const uint64_t frame = GrapeEngine::get()->timeline().getFrame();
    auto& lastToggle = p1 ? m_p1LastToggle : m_p2LastToggle;
    const bool actualHolding = holdingJump(player);
    if (clicked != actualHolding) {
        clicked = actualHolding;
        lastToggle = frame;
    }

    const int who = p1 ? Mode::Player1 : Mode::Player2;
    const bool flying = player->m_isShip || player->m_isBird ||
                        player->m_isDart || player->m_isSwing;
    const bool groundJump = !(flying || player->m_isBall ||
                              player->m_isSpider);

    auto& ts = GrapeSettings::get()->trajectory;
    const double savedLength = ts.length;
    const int savedMaxSteps = ts.maxSteps;
    ts.length = .35;
    ts.maxSteps = 0;
    const int horizon = std::max(1, traj->getPredictionLength(pl));
    const auto predict = [&](bool hold) {
        const int mode = who | (hold ? Mode::Hold : Mode::Release);
        return traj->simulate(pl, p1, mode, false,
                              {.m_bypassConfig = true});
    };

    const auto nearby = inspectNearby(pl, player);
    const auto holdPrediction = predict(true);
    const auto releasePrediction = predict(false);
    const int holdScore = holdPrediction.score;
    const int releaseScore = releasePrediction.score;
    const int currentScore = clicked ? holdScore : releaseScore;
    const int alternativeScore = clicked ? releaseScore : holdScore;
    const double tps = std::max(1.0, GrapeEngine::get()->timeline().getTps());
    const double reactionTime = flying ? .08 : (player->m_isRobot ? .20 : .16);
    const int reactionFrames = std::min(
        horizon,
        std::clamp(static_cast<int>(std::lround(tps * reactionTime)), 6, 48));
    const float hazardDistance =
        std::abs(static_cast<float>(player->m_playerSpeed)) *
            static_cast<float>(reactionTime) + 8.0f;
    const float blockDistance =
        FrameEngine::playerSpeedUnits(player->m_playerSpeed) *
            static_cast<float>(reactionTime) + 8.0f;
    const float nearestThreat = std::min(nearby.hazard, nearby.block);
    const bool orbNeeded = !clicked && nearby.ring <= 45.0f &&
        (holdScore > releaseScore || nearestThreat <= 160.0f);
    const bool lateJump = !clicked && groundJump &&
        shouldJump(nearby.hazard, nearby.block, hazardDistance, blockDistance);
    const bool lowerEmergency = flying && nearby.lowerClearance <= 12.0f &&
        nearby.lowerClearance + 2.0f < nearby.upperClearance;
    const bool upperEmergency = flying && nearby.upperClearance <= 12.0f &&
        nearby.upperClearance + 2.0f < nearby.lowerClearance;

    bool wantHold = clicked;
    if (lastToggle == UINT64_MAX) lastToggle = frame;
    const int minimumFrames = m_proportional
        ? timingFrames(std::max(2, m_cycleFrames),
                       clicked ? m_holdPct : m_releasePct)
        : (clicked && player->m_isRobot
               ? std::clamp(static_cast<int>(std::lround(tps * .12)), 8, 36)
               : (clicked && groundJump ? 2 : 1));
    if (lowerEmergency)
        wantHold = !player->m_isUpsideDown;
    else if (upperEmergency)
        wantHold = player->m_isUpsideDown;
    else if (orbNeeded || lateJump)
        wantHold = true;
    else if (shouldEscape(currentScore, alternativeScore, reactionFrames))
        wantHold = !clicked;
    else if (player->m_isShip && holdScore == releaseScore &&
             frame - lastToggle >= static_cast<uint64_t>(minimumFrames)) {
        const double velocity = player->m_yVelocity;
        if ((!player->m_isUpsideDown && velocity < -1.0) ||
            (player->m_isUpsideDown && velocity > 1.0))
            wantHold = true;
        else if ((!player->m_isUpsideDown && velocity > 1.0) ||
                 (player->m_isUpsideDown && velocity < -1.0))
            wantHold = false;
    }
    else if (clicked && groundJump &&
             frame - lastToggle >= static_cast<uint64_t>(minimumFrames) &&
             releaseScore >= holdScore && nearby.ring > 45.0f)
        wantHold = false;

    ts.length = savedLength;
    ts.maxSteps = savedMaxSteps;

    if (wantHold != clicked) {
        auto& lastToggle = p1 ? m_p1LastToggle : m_p2LastToggle;
        auto& swiftAcc = p1 ? m_p1SwiftAcc : m_p2SwiftAcc;
        clicked = wantHold;
        pl->queueButton(1, clicked, !p1, 0.0);
        lastToggle = frame;
        if (clicked && groundJump && nearby.ring <= 1.0f &&
            m_proportional && m_swiftPct > 0.0f) {
            swiftAcc += m_swiftPct;
            if (swiftAcc >= 100.0f) {
                swiftAcc -= 100.0f;
                clicked = false;
                pl->queueButton(1, false, !p1, 0.0);
            }
        }
    }
}

void Pathfinder::update(PlayLayer* pl) {
    if (!pl || !m_enabled->inner()) return;

    auto bot = GrapeEngine::get();
    if (!bot->isRecording()) return;  // only steer during live play
    if (!pl->m_player1) return;

    const bool dead = pl->m_player1->m_isDead ||
                      (pl->m_player2 && pl->m_player2->m_isDead);
    if (dead) {
        // Instant respawn: restart the moment we die instead of sitting through
        // the death animation. resetLevel() runs the normal reset path (which
        // also calls onDeath for learning). Guarded so it fires once per death;
        // the delayedResetLevel hook skips its own reset while we're alive so
        // this doesn't double-restart.
        if (!m_awaitingRespawn) {
            m_awaitingRespawn = true;
            pl->resetLevel();
        }
        return;
    }
    m_awaitingRespawn = false;

    auto traj = bot->trajectory().unsafeInner();
    if (!traj) return;

    const uint64_t frame = bot->timeline().getFrame();
    if (frame == m_lastFrame) return;  // one decision per frame
    m_lastFrame = frame;

    decide(pl, traj, true, m_p1Clicked);
    if (pl->m_gameState.m_isDualMode && pl->m_player2)
        decide(pl, traj, false, m_p2Clicked);
}

void Pathfinder::onDeath(PlayLayer*, uint64_t deathFrame) {
    if (!m_enabled->inner()) return;

    // Frames within this window of the previous death count as "the same
    // spot" -- repeated deaths there mean we're stuck.
    constexpr uint64_t tolerance = 30;
    const uint64_t delta =
        m_lastDeathFrame == UINT64_MAX
            ? UINT64_MAX
            : (deathFrame > m_lastDeathFrame ? deathFrame - m_lastDeathFrame
                                             : m_lastDeathFrame - deathFrame);
    if (delta <= tolerance)
        m_consecutiveDeaths++;
    else
        m_consecutiveDeaths = 1;
    m_lastDeathFrame = deathFrame;

    if (m_consecutiveDeaths >= std::max(1, m_stuckDeaths)) {
        // Drop the checkpoint we keep dying just past so the upcoming restart
        // (handled by resetLevel right after this) resumes from an earlier
        // point. Safe to call when there are no checkpoints -- it no-ops.
        GrapeEngine::get()->practiceFix().popLatest();
        m_consecutiveDeaths = 0;
        m_lastDeathFrame = UINT64_MAX;
    }

    m_p1Clicked = false;
    m_p2Clicked = false;
    m_p1LastToggle = UINT64_MAX;
    m_p2LastToggle = UINT64_MAX;
    m_p1SwiftAcc = 0.0f;
    m_p2SwiftAcc = 0.0f;
    m_lastFrame = UINT64_MAX;
}
