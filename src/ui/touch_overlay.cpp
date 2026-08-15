#include "touch_overlay.hpp"
#include "config/config.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"

using namespace geode::prelude;

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

    auto* background = CCLayerColor::create(ccc4(4, 10, 38, 180), 148.f, 54.f);
    background->setPosition(ccp(8.f, 8.f));
    this->addChild(background);

    auto* leftLabel = CCLabelBMFont::create("<", "bigFont.fnt");
    leftLabel->setScale(.55f);
    m_leftSprite = CircleButtonSprite::create(
        leftLabel, CircleBaseColor::Cyan, CircleBaseSize::Small);
    m_leftBtn = CCMenuItemSpriteExtra::create(
        m_leftSprite, this, menu_selector(TouchOverlay::onLeft)
    );

    m_toggleLabel = CCLabelBMFont::create(">", "bigFont.fnt");
    m_toggleLabel->setScale(.55f);
    m_toggleSprite = CircleButtonSprite::create(
        m_toggleLabel, CircleBaseColor::Green, CircleBaseSize::Medium);
    m_toggleBtn = CCMenuItemSpriteExtra::create(
        m_toggleSprite, this, menu_selector(TouchOverlay::onToggle)
    );

    auto* rightLabel = CCLabelBMFont::create(">", "bigFont.fnt");
    rightLabel->setScale(.55f);
    m_rightSprite = CircleButtonSprite::create(
        rightLabel, CircleBaseColor::Cyan, CircleBaseSize::Small);
    m_rightBtn = CCMenuItemSpriteExtra::create(
        m_rightSprite, this, menu_selector(TouchOverlay::onRight)
    );

    m_menu = CCMenu::create();
    m_menu->addChild(m_leftBtn);
    m_menu->addChild(m_toggleBtn);
    m_menu->addChild(m_rightBtn);

    m_leftBtn->setPosition(ccp(34.f, 35.f));
    m_toggleBtn->setPosition(ccp(82.f, 35.f));
    m_rightBtn->setPosition(ccp(130.f, 35.f));

    m_menu->setPosition(CCPointZero);
    this->addChild(m_menu);

    this->setVisible(false);
    return true;
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
    m_toggleLabel->setString(timeline.isPaused() ? ">" : "||");
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
    m_leftSprite->setOpacity(opacity);
    m_toggleSprite->setOpacity(opacity);
    m_rightSprite->setOpacity(opacity);
    m_toggleLabel->setString(timeline.isPaused() ? ">" : "||");
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
