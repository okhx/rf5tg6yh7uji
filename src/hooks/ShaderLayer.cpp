#include <Geode/Geode.hpp>

#include "../engine/engine.hpp"
#include "engine/timeline.hpp"

using namespace geode::prelude;

#include <Geode/modify/ShaderLayer.hpp>

struct GrapeShaderLayer : Modify<GrapeShaderLayer, ShaderLayer> {
    void performCalculations() {
        if (GrapeEngine::get()->isEnabled() &&
            GrapeEngine::get()->timeline().m_layoutMode->inner() &&
            !LevelEditorLayer::get()) {
            m_state.m_usesShaders = false;
            return;
        }

        ShaderLayer::performCalculations();
    }
};
