#include <Geode/Geode.hpp>
#include <Geode/binding/EditorPauseLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

#include "assist/hitboxes.hpp"
#include "engine/engine.hpp"
#include "trajectory/trajectory.hpp"

using namespace geode::prelude;

struct GrapeEditorPauseLayer : Modify<GrapeEditorPauseLayer, EditorPauseLayer> {
    void onSaveAndPlay(cocos2d::CCObject* sender) {
        GrapeEngine::get()->hitboxes().destroy();
        GrapeEngine::get()->trajectory().uninit();

        EditorPauseLayer::onSaveAndPlay(sender);
    }
};
