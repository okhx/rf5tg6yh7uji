#ifndef TOUCH_OVERLAY_HPP
#define TOUCH_OVERLAY_HPP

#include <Geode/Geode.hpp>

class TouchOverlay : public cocos2d::CCLayer {
protected:
    CCMenuItemSpriteExtra* m_leftBtn;
    CCMenuItemSpriteExtra* m_rightBtn;
    cocos2d::CCMenu* m_menu;

    bool init() override;

public:
    static TouchOverlay* create();
    static TouchOverlay* get();

    void onLeft(cocos2d::CCObject*);
    void onRight(cocos2d::CCObject*);

    void updateVisibility();
    void hide();
    void show();
};

#endif // TOUCH_OVERLAY_HPP
