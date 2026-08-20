#include <Geode/Geode.hpp>
#include <Geode/binding/EditorPauseLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

using namespace geode::prelude;

struct GrapeEditorPauseLayer : Modify<GrapeEditorPauseLayer, EditorPauseLayer> {
    void onSaveAndPlay(cocos2d::CCObject* sender) {
        EditorPauseLayer::onSaveAndPlay(sender);
    }
};
