#include "touch_overlay.hpp"
#include "bot/bot.hpp"
#include "bot/updater.hpp"

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

    auto leftSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    leftSprite->setFlipX(true);
    leftSprite->setOpacity(150);
    m_leftBtn = CCMenuItemSpriteExtra::create(
        leftSprite, this, menu_selector(TouchOverlay::onLeft)
    );
    
    auto rightSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    rightSprite->setOpacity(150);
    m_rightBtn = CCMenuItemSpriteExtra::create(
        rightSprite, this, menu_selector(TouchOverlay::onRight)
    );

    m_menu = CCMenu::create();
    m_menu->addChild(m_leftBtn);
    m_menu->addChild(m_rightBtn);
    
    // Position arrows on edges
    m_leftBtn->setPosition(ccp(-winSize.width / 2.f + 40.f, 0.f));
    m_rightBtn->setPosition(ccp(winSize.width / 2.f - 40.f, 0.f));

    m_menu->setPosition(winSize / 2.f);
    this->addChild(m_menu);

    this->setTouchEnabled(true);
    // Highest priority so it gets touches before game layer
    CCTouchDispatcher::get()->addTargetedDelegate(this, -500, true);

    this->setVisible(false);

    return true;
}

void TouchOverlay::onLeft(CCObject*) {
    Bot::get()->updater().backwardsStep();
}

void TouchOverlay::onRight(CCObject*) {
    Bot::get()->updater().stepOnce();
}

void TouchOverlay::updateVisibility() {
    auto playLayer = PlayLayer::get();
    bool shouldBeVisible = playLayer && Bot::get()->updater().isPaused();
    this->setVisible(shouldBeVisible);
}

void TouchOverlay::hide() {
    this->setVisible(false);
}

void TouchOverlay::show() {
    this->setVisible(true);
    Bot::get()->updater().m_paused->inner() = true;
    Bot::get()->updater().m_paused->notifyChange();
}
