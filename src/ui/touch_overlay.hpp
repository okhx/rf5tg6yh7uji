#ifndef TOUCH_OVERLAY_HPP
#define TOUCH_OVERLAY_HPP

#include <Geode/Geode.hpp>

class TouchOverlay : public cocos2d::CCLayer {
protected:
    cocos2d::CCSprite* m_leftArrow = nullptr;
    cocos2d::CCSprite* m_rightArrow = nullptr;
    cocos2d::CCSprite* m_toggleSprite = nullptr;

    CCMenuItemSpriteExtra* m_leftBtn = nullptr;
    CCMenuItemSpriteExtra* m_toggleBtn = nullptr;
    CCMenuItemSpriteExtra* m_rightBtn = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;

    bool m_active = false;
    bool m_togglePaused = false;
    float m_arrowOpacity = -1.f;

    float m_leftHeld = 0.f;
    float m_rightHeld = 0.f;
    float m_leftRepeat = 0.f;
    float m_rightRepeat = 0.f;

    bool init() override;

    void redrawArrows(float alpha);
    void redrawToggle(bool paused);

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
