#include "autoclicker.hpp"

#include "assist/nearby_objects.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "replay/macro.hpp"

#include <limits>
#include <optional>

namespace {
constexpr bool steerByClearance(float playerBottom, float playerTop,
                                float gapBottom, float gapTop,
                                bool upsideDown, bool pressed) {
    const float lower = playerBottom - gapBottom;
    const float upper = gapTop - playerTop;
    // The deadband must never dominate the available clearance, otherwise a
    // razor-thin gap leaves the controller unable to pick a side. Scale the
    // floor down with the total clearance so it stays a fraction of it.
    const float total = lower + upper;
    const float floor = std::min(.25f, std::max(0.0f, total) * .5f);
    const float deadband = std::clamp(total * .12f, floor, 1.5f);
    if (lower + deadband < upper) return !upsideDown;
    if (upper + deadband < lower) return upsideDown;
    return !pressed;
}

constexpr float distanceAhead(float x, float minimum, float maximum,
                              float direction) {
    if ((direction > 0 && maximum < x - 20.0f) ||
        (direction < 0 && minimum > x + 20.0f))
        return -1.0f;
    return direction > 0 ? std::max(0.0f, minimum - x)
                         : std::max(0.0f, x - maximum);
}

constexpr float futureGapOffset(float velocity, float acceleration,
                                int frames) {
    const float linear = velocity * frames;
    const float curve = acceleration * frames * frames * .125f;
    const float limit = (linear < 0.0f ? -linear : linear) * .5f;
    return linear + std::clamp(curve, -limit, limit);
}

constexpr bool intervalElapsed(uint64_t frame, uint64_t last, int interval) {
    return last == UINT64_MAX || frame < last ||
           frame - last >= static_cast<uint64_t>(interval);
}

static_assert(steerByClearance(4, 14, 0, 40, false, false));
static_assert(!steerByClearance(26, 36, 0, 40, false, true));
static_assert(steerByClearance(15, 25, 0, 40, false, false));
static_assert(distanceAhead(100, 50, 150, 1) == 0);
static_assert(distanceAhead(100, 130, 150, 1) == 30);
static_assert(futureGapOffset(2, 1, 2) == 4.5f);
static_assert(intervalElapsed(11, 10, 1));
static_assert(!intervalElapsed(11, 10, 2));

bool holdingJump(PlayerObject* player) {
    if (!player) return false;
    const auto held = player->m_holdingButtons.find(
        static_cast<int>(PlayerButton::Jump));
    return held != player->m_holdingButtons.end() && held->second;
}

bool touchingRing(PlayerObject* player) {
    return player && player->m_touchingRings &&
           player->m_touchingRings->count() != 0;
}

struct GapBounds {
    float center;
    float lower;
    float upper;
};

std::optional<GapBounds> movingGap(PlayLayer* layer, PlayerObject* player,
                                   float referenceY) {
    const float x = player->getPositionX();
    const float direction = player->m_isGoingLeft ? -1.0f : 1.0f;
    float lowerScore = std::numeric_limits<float>::max();
    float upperScore = std::numeric_limits<float>::max();
    float lower = 0.0f;
    float upper = 0.0f;
    grape::assist::forEachNearbyObject(layer, [&](GameObject* object) {
        if (object->m_objectType != GameObjectType::Solid &&
            object->m_objectType != GameObjectType::Breakable &&
            object->m_objectType != GameObjectType::Hazard &&
            object->m_objectType != GameObjectType::AnimatedHazard)
            return;
        const auto rect = object->getObjectRect();
        const float ahead = distanceAhead(
            x, rect.getMinX(), rect.getMaxX(), direction);
        if (ahead < 0.0f || ahead > 180.0f) return;
        // Tighter classification tolerance (was 3.0) so an object bordering a
        // small gap can't be counted as both the floor and the ceiling.
        if (rect.getMaxY() <= referenceY + 1.5f) {
            const float score = std::max(0.0f, referenceY - rect.getMaxY()) +
                                ahead * .1f;
            if (score >= lowerScore) return;
            lowerScore = score;
            lower = rect.getMaxY();
        } else if (rect.getMinY() >= referenceY - 1.5f) {
            const float score = std::max(0.0f, rect.getMinY() - referenceY) +
                                ahead * .1f;
            if (score >= upperScore) return;
            upperScore = score;
            upper = rect.getMinY();
        }
    });
    if (lowerScore == std::numeric_limits<float>::max() ||
        upperScore == std::numeric_limits<float>::max())
        return std::nullopt;
    if (upper <= lower) {
        // Degenerate/inverted detection on a very tight gap. Collapse to a
        // point gap at the midpoint so the tracker keeps its lock instead of
        // dropping to dead-reckoning, which is far less accurate here.
        const float mid = (lower + upper) * .5f;
        return GapBounds{mid, mid, mid};
    }
    return GapBounds{(lower + upper) * .5f, lower, upper};
}

}

void Autoclicker::update(PlayLayer* pl) {
    if (!pl) {
        m_p1Clicked = false;
        m_p2Clicked = false;
        return;
    }

    auto bot = GrapeEngine::get();

    if (!m_enabled->inner()) {
        if (m_p1Clicked) {
            pl->queueButton(1, false, false, 0.0);
            m_p1Clicked = false;
        }
        if (m_p2Clicked) {
            pl->queueButton(1, false, true, 0.0);
            m_p2Clicked = false;
        }
        return;
    }

    auto frame = bot->timeline().getFrame();
    const bool independentPlayer2 = pl->m_gameState.m_isDualMode &&
                                    pl->m_player2;
    const bool doPlayer1 = !independentPlayer2 || performPlayer1();
    const bool doPlayer2 = independentPlayer2 && performPlayer2();
    if (!doPlayer1 && m_p1Clicked) {
        pl->queueButton(1, false, false, 0.0);
        m_p1Clicked = false;
    }
    if (!doPlayer2 && m_p2Clicked) {
        pl->queueButton(1, false, true, 0.0);
        m_p2Clicked = false;
    }

    if (frame == m_lastFrame) {
        return;
    }

    if (!bot->isRecording()) {
        if (m_p1Clicked)
            pl->queueButton(1, false, false, 0.0);
        if (m_p2Clicked)
            pl->queueButton(1, false, true, 0.0);
        m_p1Clicked = false;
        m_p2Clicked = false;
        return;
    }

    const auto apply = [&](bool player2, bool& pressed,
                           std::optional<bool> desired) {
        if (!desired || *desired == pressed) return false;
        pressed = *desired;
        pl->queueButton(1, pressed, player2, 0.0);
        return true;
    };

    if (m_movingGap) {
        if (doPlayer1 && pl->m_player1->m_isDart)
            m_p1Clicked = holdingJump(pl->m_player1);
        if (doPlayer2 && pl->m_player2->m_isDart)
            m_p2Clicked = holdingJump(pl->m_player2);
    }
    const auto trackGap = [&](PlayerObject* player, auto& state) {
        if (!m_movingGap || !player || player->m_isDead) {
            state = {};
            return;
        }
        const int elapsed = state.frame == UINT64_MAX || frame <= state.frame
            ? 1 : static_cast<int>(std::min<uint64_t>(frame - state.frame, 30));
        const float predictedY = player->getPositionY() +
            std::clamp(static_cast<float>(player->m_yVelocity * .03),
                       -20.0f, 20.0f);
        const float referenceY = std::isfinite(state.center)
            ? state.center + futureGapOffset(
                state.velocity, state.acceleration, elapsed)
            : predictedY;
        auto current = movingGap(pl, player, referenceY);
        if (!current && std::isfinite(state.center)) {
            current = movingGap(
                pl, player,
                state.center + futureGapOffset(
                    state.velocity, state.acceleration, elapsed + 2));
        }
        if (!current) current = movingGap(pl, player, predictedY);
        if (current) {
            const float rawVelocity = std::isfinite(state.center)
                ? (current->center - state.center) / elapsed : 0.0f;
            // Gap detection re-picks the nearest bounding objects every frame,
            // so the raw center jitters even when the true gap moves smoothly.
            // We still blend with an EMA to kill single-frame spikes, but with a
            // faster response (0.6, was 0.45): too much smoothing lags the true
            // speed of a fast gap, and an under-estimated speed makes the wave
            // react too late and clip the wall. 0.6 converges in ~3 frames while
            // still damping a lone bad sample.
            const float velocity = std::isfinite(state.center)
                ? state.velocity + (rawVelocity - state.velocity) * 0.6f
                : rawVelocity;
            const float rawAccel = (velocity - state.velocity) / elapsed;
            const float acceleration =
                state.acceleration + (rawAccel - state.acceleration) * 0.6f;
            state = {current->center, current->lower, current->upper,
                     std::clamp(velocity, -60.0f, 60.0f),
                     std::clamp(acceleration, -15.0f, 15.0f), frame};
        } else if (std::isfinite(state.center)) {
            const float offset = futureGapOffset(
                state.velocity, state.acceleration, elapsed);
            state.velocity = std::clamp(
                state.velocity + state.acceleration * elapsed, -60.0f, 60.0f);
            state.center += offset;
            state.lower += offset;
            state.upper += offset;
            state.frame = frame;
        }
    };
    if (doPlayer1) trackGap(pl->m_player1, m_p1Gap);
    if (doPlayer2) trackGap(pl->m_player2, m_p2Gap);

    const auto adaptive = [&](PlayerObject* player, bool pressed,
                              const auto& gap) -> std::optional<bool> {
        if (!m_movingGap || !player || player->m_isDead)
            return std::nullopt;
        if (!player->m_isDart) return std::nullopt;
        if (!std::isfinite(gap.center)) return std::nullopt;
        const auto hitbox = player->getObjectRect();
        const float gapHeight = gap.upper - gap.lower;
        const float playerHeight = hitbox.getMaxY() - hitbox.getMinY();
        const float freeSpace = std::max(0.0f, gapHeight - playerHeight);
        // Lower the margin floor (was .25) so tight gaps aren't treated as
        // instantly-unsafe, letting the finer reactions below do the work.
        const float marginFloor = std::min(.1f, freeSpace * .5f);
        const float margin = std::clamp(freeSpace * .15f, marginFloor, 2.0f);
        const float playerStep = std::clamp(
            static_cast<float>(player->m_yVelocity * .03), -20.0f, 20.0f);

        // The tighter the gap, the earlier we must react — there is less room
        // to correct a late input. Scale the lookahead up as free space shrinks
        // below ~6 units (roughly one player height of slack).
        int safetyFrames = std::clamp(m_movingGapLookahead, 1, 30);
        if (freeSpace < 6.0f) {
            const float tightness = 1.0f + (6.0f - freeSpace) / 6.0f;  // 1..2
            safetyFrames = std::clamp(
                static_cast<int>(safetyFrames * tightness + 0.5f), 1, 30);
        }
        // A gap sweeping quickly up or down must be met earlier: the wave climbs
        // and dives at a fixed rate, so a reaction tuned for a still gap lands
        // after the safe corridor has already slid past. Stretch the lookahead
        // with the gap's speed so the override below fires while there is still
        // room to correct.
        const float gapSpeed = std::fabs(gap.velocity);
        if (gapSpeed > 4.0f) {
            const float speedScale =
                std::min(2.25f, 1.0f + (gapSpeed - 4.0f) / 10.0f);  // 1..2.25
            safetyFrames = std::clamp(
                static_cast<int>(safetyFrames * speedScale + 0.5f), 1, 30);
        }
        const float safetyGapOffset = futureGapOffset(
            gap.velocity, gap.acceleration, safetyFrames);
        const float safetyPlayerOffset =
            playerStep * std::min(safetyFrames, 2);
        const float futureLower = hitbox.getMinY() + safetyPlayerOffset -
            (gap.lower + safetyGapOffset);
        const float futureUpper = gap.upper + safetyGapOffset -
            (hitbox.getMaxY() + safetyPlayerOffset);
        if (futureLower <= margin && futureLower < futureUpper)
            return !player->m_isUpsideDown;
        if (futureUpper <= margin && futureUpper < futureLower)
            return player->m_isUpsideDown;

        // Touching an orb while steering: a fresh press edge inside a ring can
        // activate it and hijack the trajectory. Survival (the overrides above)
        // always wins, but when the gap is otherwise safe just hold the current
        // button state so no activation edge is created. This replaces a blind
        // forced release that used to dive the wave straight into a fast gap.
        if (touchingRing(player)) return pressed;

        const float nextGapOffset = futureGapOffset(
            gap.velocity, gap.acceleration, 1);
        return steerByClearance(
            hitbox.getMinY() + playerStep,
            hitbox.getMaxY() + playerStep,
            gap.lower + nextGapOffset, gap.upper + nextGapOffset,
            player->m_isUpsideDown, pressed);
    };
    const auto p1Adaptive = doPlayer1
        ? adaptive(pl->m_player1, m_p1Clicked, m_p1Gap)
        : std::nullopt;
    const auto p2Adaptive = doPlayer2
        ? adaptive(pl->m_player2, m_p2Clicked, m_p2Gap)
        : std::nullopt;
    if (p1Adaptive)
        apply(false, m_p1Clicked, p1Adaptive);
    if (p2Adaptive)
        apply(true, m_p2Clicked, p2Adaptive);
    if ((!doPlayer1 || p1Adaptive) && (!doPlayer2 || p2Adaptive)) {
        m_lastFrame = frame;
        return;
    }

    const auto due = [&](bool pressed, uint64_t lastToggle) {
        return intervalElapsed(
            frame, lastToggle, pressed ? m_holdFrames : m_releaseFrames);
    };
    if (doPlayer1 && !p1Adaptive && due(m_p1Clicked, m_p1LastToggle)) {
        m_p1LastToggle = frame;
        if (!m_p1Clicked) {
            pl->queueButton(1, true, false, 0.0);
            m_p1Clicked = true;
            if (m_performSwifts) {
                m_p1Clicked = false;
                pl->queueButton(1, false, false, 0.0);
            }
        } else {
            m_p1Clicked = false;
            pl->queueButton(1, false, false, 0.0);
        }
    }

    if (doPlayer2 && !p2Adaptive && due(m_p2Clicked, m_p2LastToggle)) {
        m_p2LastToggle = frame;
        if (!m_p2Clicked) {
            pl->queueButton(1, true, true, 0.0);
            m_p2Clicked = true;
            if (m_performSwifts) {
                m_p2Clicked = false;
                pl->queueButton(1, false, true, 0.0);
            }
        } else {
            m_p2Clicked = false;
            pl->queueButton(1, false, true, 0.0);
        }
    }
    m_lastFrame = frame;
}
