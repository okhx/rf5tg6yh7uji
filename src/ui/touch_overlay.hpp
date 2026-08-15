#ifndef TOUCH_OVERLAY_HPP
#define TOUCH_OVERLAY_HPP

#include <Geode/Geode.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

class TouchOverlay : public cocos2d::CCLayer {
protected:
    geode::CircleButtonSprite* m_leftSprite;
    geode::CircleButtonSprite* m_toggleSprite;
    geode::CircleButtonSprite* m_rightSprite;
    cocos2d::CCLabelBMFont* m_toggleLabel;
    CCMenuItemSpriteExtra* m_leftBtn;
    CCMenuItemSpriteExtra* m_toggleBtn;
    CCMenuItemSpriteExtra* m_rightBtn;
    cocos2d::CCMenu* m_menu;
    bool m_active = false;
    float m_leftHeld = 0.f;
    float m_rightHeld = 0.f;
    float m_leftRepeat = 0.f;
    float m_rightRepeat = 0.f;

    bool init() override;

public:
    static TouchOverlay* create();
    static TouchOverlay* get();

    void update(float dt) override;

    void onLeft(cocos2d::CCObject*);
    void onToggle(cocos2d::CCObject*);
    void onRight(cocos2d::CCObject*);

    void updateVisibility();
    void hide();
    void show();
};

#endif
