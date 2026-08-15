#include "touch_overlay.hpp"
#include "config/config.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace {
// Rounded panel in the bottom-left corner.
constexpr float kBoxWidth = 160.f;
constexpr float kBoxHeight = 56.f;
constexpr float kCornerRadius = 10.f;
constexpr float kBoxX = 8.f;
constexpr float kBoxY = 8.f;

// Buttons are centered inside the panel, which sits at (kBoxX, kBoxY).
constexpr float kLeftX = kBoxX + 32.f;
constexpr float kToggleX = kBoxX + kBoxWidth * 0.5f;
constexpr float kRightX = kBoxX + kBoxWidth - 32.f;
constexpr float kCenterY = kBoxY + kBoxHeight * 0.5f;

// Blue arrow geometry (drawn programmatically, matching the level-list arrow).
constexpr float kArrowWidth = 20.f;
constexpr float kArrowHeight = 28.f;
constexpr cocos2d::ccColor4F kArrowFill = {0.16f, 0.48f, 0.90f, 1.0f};

constexpr cocos2d::ccColor4F kBoxFill = {0.0f, 0.0f, 0.0f, 0.8f};
constexpr cocos2d::ccColor4F kTransparent = {0.0f, 0.0f, 0.0f, 0.0f};

constexpr float kPi = 3.14159265358979f;

// Builds the perimeter of a rounded rectangle, tracing each corner arc in
// order so the straight edges are implied by the connections between arcs.
std::vector<cocos2d::CCPoint> roundedRectPoints(float w, float h, float r,
                                                int segments) {
    std::vector<cocos2d::CCPoint> pts;
    const auto addArc = [&](float cx, float cy, float start, float end) {
        for (int i = 0; i <= segments; ++i) {
            const float a = start + (end - start) * i / segments;
            pts.push_back(cocos2d::CCPoint(cx + r * std::cos(a),
                                            cy + r * std::sin(a)));
        }
    };
    addArc(w - r, h - r, 0.f, kPi * 0.5f);       // top-right
    addArc(r, h - r, kPi * 0.5f, kPi);           // top-left
    addArc(r, r, kPi, kPi * 1.5f);               // bottom-left
    addArc(w - r, r, kPi * 1.5f, kPi * 2.f);     // bottom-right
    return pts;
}
}  // namespace

static TouchOverlay* g_instance = nullptr;

TouchOverlay* TouchOverlay::get() {
    if (!g_instance) {
        g_instance = TouchOverlay::create();
        g_instance->retain();
    }
    return g_instance;
}

TouchOverlay* TouchOverlay::create() {
    auto ret = new TouchOverlay();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TouchOverlay::init() {
    if (!CCLayer::init()) return false;

    // Semi-transparent black panel with rounded corners, drawn
    // programmatically so it can't break on missing textures.
    auto* background = CCDrawNode::create();
    auto pts = roundedRectPoints(kBoxWidth, kBoxHeight, kCornerRadius, 6);
    background->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()),
                            kBoxFill, 0.f, kTransparent);
    background->setContentSize({kBoxWidth, kBoxHeight});
    background->setAnchorPoint(CCPointZero);
    background->setPosition(ccp(kBoxX, kBoxY));
    this->addChild(background);

    // Blue chevron arrows, drawn programmatically.
    m_leftArrow = CCDrawNode::create();
    m_leftArrow->setContentSize({kArrowWidth, kArrowHeight});
    m_leftArrow->setAnchorPoint(ccp(0.5f, 0.5f));
    m_leftBtn = CCMenuItemSpriteExtra::create(
        m_leftArrow, this, menu_selector(TouchOverlay::onLeft));

    m_rightArrow = CCDrawNode::create();
    m_rightArrow->setContentSize({kArrowWidth, kArrowHeight});
    m_rightArrow->setAnchorPoint(ccp(0.5f, 0.5f));
    m_rightBtn = CCMenuItemSpriteExtra::create(
        m_rightArrow, this, menu_selector(TouchOverlay::onRight));

    // Editor play/stop button for the pause/play toggle.
    m_toggleSprite =
        CCSprite::createWithSpriteFrameName("GJ_playEditorBtn_001.png");
    m_toggleBtn = CCMenuItemSpriteExtra::create(
        m_toggleSprite, this, menu_selector(TouchOverlay::onToggle));

    m_menu = CCMenu::create();
    m_menu->addChild(m_leftBtn);
    m_menu->addChild(m_toggleBtn);
    m_menu->addChild(m_rightBtn);

    m_leftBtn->setSizeMult(1.5f);
    m_toggleBtn->setSizeMult(1.5f);
    m_rightBtn->setSizeMult(1.5f);

    m_leftBtn->setPosition(ccp(kLeftX, kCenterY));
    m_toggleBtn->setPosition(ccp(kToggleX, kCenterY));
    m_rightBtn->setPosition(ccp(kRightX, kCenterY));

    m_menu->setPosition(CCPointZero);
    this->addChild(m_menu);

    const float opacity = static_cast<float>(std::clamp(
        GrapeSettings::get()->frameStepperArrowOpacity, 0.1, 1.0));
    m_arrowOpacity = opacity;
    this->redrawArrows(opacity);

    m_togglePaused = true;
    this->redrawToggle(true);

    this->setVisible(false);
    return true;
}

void TouchOverlay::redrawArrows(float alpha) {
    const float hw = kArrowWidth * 0.5f;
    const float hh = kArrowHeight * 0.5f;
    const cocos2d::ccColor4F fill = {kArrowFill.r, kArrowFill.g, kArrowFill.b,
                                     alpha};

    const auto drawOne = [&](CCDrawNode* node, bool left) {
        if (!node) return;
        node->clear();
        const float dir = left ? -1.f : 1.f;
        cocos2d::CCPoint tri[3] = {
            cocos2d::CCPoint(-dir * hw, -hh),
            cocos2d::CCPoint(-dir * hw, hh),
            cocos2d::CCPoint(dir * hw, 0.f),
        };
        node->drawPolygon(tri, 3, fill, 0.f, kTransparent);
    };

    drawOne(m_leftArrow, true);
    drawOne(m_rightArrow, false);
}

void TouchOverlay::redrawToggle(bool paused) {
    if (!m_toggleSprite) return;
    m_toggleSprite->setDisplayFrame(
        CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(
            paused ? "GJ_playEditorBtn_001.png"
                   : "GJ_stopEditorBtn_001.png"));
}

void TouchOverlay::update(float dt) {
    auto* settings = GrapeSettings::get();
    if (!settings->frameStepperHold || !this->isVisible()) {
        m_leftHeld = m_rightHeld = m_leftRepeat = m_rightRepeat = 0.f;
        return;
    }

    const float delay = std::max(0.0, settings->frameStepperHoldDelay);
    const float interval =
        1.f / std::max(1.0, settings->frameStepperHoldSpeed);
    const auto repeat = [dt, delay, interval](CCMenuItemSpriteExtra* button,
                                               float& held, float& elapsed,
                                               auto action) {
        if (!button->isSelected()) {
            held = elapsed = 0.f;
            return;
        }
        held += dt;
        if (held < delay) return;
        elapsed += dt;
        while (elapsed >= interval) {
            elapsed -= interval;
            action();
        }
    };

    repeat(m_leftBtn, m_leftHeld, m_leftRepeat,
           [] { GrapeEngine::get()->timeline().backwardsStep(); });
    repeat(m_rightBtn, m_rightHeld, m_rightRepeat,
           [] { GrapeEngine::get()->timeline().stepOnce(); });
}

void TouchOverlay::onLeft(CCObject*) {
    auto& timeline = GrapeEngine::get()->timeline();
    if (timeline.m_backwardsStepping->inner()) timeline.backwardsStep();
}

void TouchOverlay::onToggle(CCObject*) {
    auto& timeline = GrapeEngine::get()->timeline();
    timeline.setPaused(!timeline.isPaused());
    timeline.m_paused->notifyChange();
    const bool paused = timeline.isPaused();
    m_togglePaused = paused;
    this->redrawToggle(paused);
}

void TouchOverlay::onRight(CCObject*) {
    GrapeEngine::get()->timeline().stepOnce();
}

void TouchOverlay::updateVisibility() {
    auto* playLayer = PlayLayer::get();
    auto& timeline = GrapeEngine::get()->timeline();
    if (playLayer && this->getParent() != playLayer) {
        this->removeFromParentAndCleanup(false);
        playLayer->addChild(this, 1000);
    }
    if (timeline.isPaused()) m_active = true;

    const float opacity = static_cast<float>(std::clamp(
        GrapeSettings::get()->frameStepperArrowOpacity, 0.1, 1.0));
    if (opacity != m_arrowOpacity) {
        m_arrowOpacity = opacity;
        this->redrawArrows(opacity);
    }

    const bool paused = timeline.isPaused();
    if (paused != m_togglePaused) {
        m_togglePaused = paused;
        this->redrawToggle(paused);
    }

    m_leftBtn->setVisible(timeline.m_backwardsStepping->inner());

#ifdef GEODE_IS_IOS
    bool isRendering = Renderer::get()->isRecording();
    this->setVisible(playLayer && m_active && !isRendering);
#else
    this->setVisible(playLayer && m_active);
#endif
}

void TouchOverlay::hide() {
    m_active = false;
    this->setVisible(false);
}

void TouchOverlay::show() {
    m_active = true;
    this->setVisible(true);
    auto& timeline = GrapeEngine::get()->timeline();
    timeline.m_paused->inner() = true;
    timeline.m_paused->notifyChange();
    m_togglePaused = true;
    this->redrawToggle(true);
}
