#include "touch_overlay.hpp"
#include "config/config.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"

#include <algorithm>
#include <array>

using namespace geode::prelude;

namespace {
constexpr float kBoxWidth = 148.f;
constexpr float kBoxHeight = 54.f;

constexpr float kLeftX = 34.f;
constexpr float kToggleX = 82.f;
constexpr float kRightX = 130.f;
constexpr float kCenterY = 35.f;

constexpr float kCircleRadius = 16.f;
constexpr cocos2d::ccColor4F kCircleFill = {0.16f, 0.48f, 0.90f, 1.0f};
constexpr cocos2d::ccColor4F kCircleLine = {0.85f, 0.92f, 1.0f, 0.9f};
constexpr cocos2d::ccColor4F kIconFill = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr cocos2d::ccColor4F kTransparent = {0.0f, 0.0f, 0.0f, 0.0f};
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

    // Semi-transparent black panel in the bottom-left corner.
    auto* background =
        CCLayerColor::create(ccc4(0, 0, 0, 204), kBoxWidth, kBoxHeight);
    background->setPosition(ccp(8.f, 8.f));
    this->addChild(background);

    // Blue left/right arrows straight from the game atlas.
    m_leftArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    if (m_leftArrow) m_leftArrow->setFlipX(true);
    m_leftBtn = CCMenuItemSpriteExtra::create(
        m_leftArrow, this, menu_selector(TouchOverlay::onLeft));

    m_rightArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    m_rightBtn = CCMenuItemSpriteExtra::create(
        m_rightArrow, this, menu_selector(TouchOverlay::onRight));

    // Center circular play/pause button, drawn programmatically so it can't
    // break on missing textures.
    m_toggleCircle = CCDrawNode::create();
    m_toggleCircle->setContentSize(
        {kCircleRadius * 2.f, kCircleRadius * 2.f});
    m_toggleBtn = CCMenuItemSpriteExtra::create(
        m_toggleCircle, this, menu_selector(TouchOverlay::onToggle));

    m_menu = CCMenu::create();
    m_menu->addChild(m_leftBtn);
    m_menu->addChild(m_toggleBtn);
    m_menu->addChild(m_rightBtn);

    m_leftBtn->setSizeMult(2.0f);
    m_rightBtn->setSizeMult(2.0f);

    m_leftBtn->setPosition(ccp(kLeftX, kCenterY));
    m_toggleBtn->setPosition(ccp(kToggleX, kCenterY));
    m_rightBtn->setPosition(ccp(kRightX, kCenterY));

    m_menu->setPosition(CCPointZero);
    this->addChild(m_menu);

    m_togglePaused = true;
    this->redrawToggle(true);

    this->setVisible(false);
    return true;
}

void TouchOverlay::redrawToggle(bool paused) {
    if (!m_toggleCircle) return;

    m_toggleCircle->clear();

    const float r = kCircleRadius;
    m_toggleCircle->drawCircle(ccp(0.f, 0.f), r, kCircleFill, 1.5f, kCircleLine,
                               48);

    if (paused) {
        // Play triangle pointing right.
        std::array<cocos2d::CCPoint, 3> tri = {
            cocos2d::CCPoint{-r * 0.4f, -r * 0.5f},
            cocos2d::CCPoint{-r * 0.4f, r * 0.5f},
            cocos2d::CCPoint{r * 0.6f, 0.f},
        };
        m_toggleCircle->drawPolygon(tri.data(), tri.size(), kIconFill, 0.f,
                                    kTransparent);
    } else {
        // Pause bars.
        const float bw = r * 0.26f;
        const float bh = r * 1.0f;
        m_toggleCircle->drawRect({-bw * 1.5f, -bh * 0.5f, bw, bh}, kIconFill,
                                 0.f, kTransparent);
        m_toggleCircle->drawRect({bw * 0.5f, -bh * 0.5f, bw, bh}, kIconFill,
                                 0.f, kTransparent);
    }
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
    const auto opacity = static_cast<GLubyte>(std::clamp(
        GrapeSettings::get()->frameStepperArrowOpacity, 0.1, 1.0) * 255.0);
    if (m_leftArrow) m_leftArrow->setOpacity(opacity);
    if (m_rightArrow) m_rightArrow->setOpacity(opacity);

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
    GrapeEngine::get()->timeline().m_paused->inner() = true;
    GrapeEngine::get()->timeline().m_paused->notifyChange();
}
