#include "touch_overlay.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"

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

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    auto leftSprite = ButtonSprite::create(
        "<", 34, true, "bigFont.fnt", "GJ_button_01.png", 22.f, .65f);
    m_leftBtn = CCMenuItemSpriteExtra::create(
        leftSprite, this, menu_selector(TouchOverlay::onLeft)
    );
    
    auto rightSprite = ButtonSprite::create(
        ">", 34, true, "bigFont.fnt", "GJ_button_01.png", 22.f, .65f);
    m_rightBtn = CCMenuItemSpriteExtra::create(
        rightSprite, this, menu_selector(TouchOverlay::onRight)
    );

    m_menu = CCMenu::create();
    m_menu->addChild(m_leftBtn);
    m_menu->addChild(m_rightBtn);
    
    m_leftBtn->setPosition(ccp(-winSize.width / 2.f + 40.f, 0.f));
    m_rightBtn->setPosition(ccp(winSize.width / 2.f - 40.f, 0.f));

    m_menu->setPosition(winSize / 2.f);
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
    GrapeEngine::get()->timeline().backwardsStep();
}

void TouchOverlay::onRight(CCObject*) {
    GrapeEngine::get()->timeline().stepOnce();
}

void TouchOverlay::updateVisibility() {
    auto playLayer = PlayLayer::get();
    bool shouldBeVisible = playLayer && GrapeEngine::get()->timeline().isPaused();
    this->setVisible(shouldBeVisible);
}

void TouchOverlay::hide() {
    this->setVisible(false);
}

void TouchOverlay::show() {
    this->setVisible(true);
    GrapeEngine::get()->timeline().m_paused->inner() = true;
    GrapeEngine::get()->timeline().m_paused->notifyChange();
}
