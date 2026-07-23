#include "label.hpp"
#include "util/storage.hpp"

#include <glaze/glaze.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "replay/macro.hpp"

using namespace geode::prelude;

static std::string resolveFontFile(const Label::LabelConfig& cfg) {
    switch (cfg.m_font) {
        case Label::LabelFont::BigFont:  return "bigFont.fnt";
        case Label::LabelFont::ChatFont: return "chatFont.fnt";
    }
    return "chatFont.fnt";
}

#define GET_PLAYER_VAR(display, var, precision)                             \
    {                                                                       \
        auto pl = PlayLayer::get();                                         \
        auto player = pl->m_player1;                                        \
        auto player2 = pl->m_player2;                                       \
        if (pl->m_gameState.m_isDualMode && player2) {                      \
            return fmt::format("{}: {:." #precision "f} / {:." #precision   \
                               "f}",                                        \
                               display, player->m_##var, player2->m_##var); \
        } else {                                                            \
            return fmt::format("{}: {:." #precision "f}", display,          \
                               player->m_##var);                            \
        }                                                                   \
    }

LabelManager::LabelManager() {
    addLabel("frame", "Tick",
             []() {
                 auto& updater = GrapeEngine::get()->timeline();
                 return fmt::format(
                     "Tick: {}", static_cast<uint64_t>(updater.getFrame()) * 2);
             },
             {});

    addLabel("internal_frame", "Internal Physics Tick",
             []() {
                 return fmt::format(
                     "Internal Tick: {}",
                     PlayLayer::get()->m_gameState.m_currentProgress);
             },
             {});

    addLabel(
        "tps", "TPS",
        []() { return fmt::format("TPS: {}", GrapeEngine::get()->timeline().getTps()); },
        {});

    addLabel("player_x", "Player X",
             []() { GET_PLAYER_VAR("X", position.x, 6) }, {});
    addLabel("player_y", "Player Y",
             []() { GET_PLAYER_VAR("Y", position.y, 6) }, {});
    addLabel("player_xvel", "Player X Velocity",
             []() { GET_PLAYER_VAR("X Velocity", platformerXVelocity, 6) }, {});
    addLabel("player_yvel", "Player Y Velocity",
             []() { GET_PLAYER_VAR("Y Velocity", yVelocity, 3) }, {});
    addLabel("player_rot", "Player Rotation",
             []() { GET_PLAYER_VAR("Rotation", fRotationX, 3) }, {});
    addLabel("player_speed", "Player Speed",
             []() { GET_PLAYER_VAR("Speed", playerSpeed, 2) }, {});

    addLabel("player_grav", "Player Gravity",
             []() {
                 auto pl = PlayLayer::get();
                 auto player = pl->m_player1;
                 auto player2 = pl->m_player2;
                 if (pl->m_gameState.m_isDualMode && player2) {
                     return fmt::format(
                         "{}: {} / {}", "Gravity",
                         player->m_isUpsideDown ? "Flipped" : "Normal",
                         player2->m_isUpsideDown ? "Flipped" : "Normal");
                 } else {
                     return fmt::format(
                         "{}: {}", "Gravity",
                         player->m_isUpsideDown ? "Flipped" : "Normal");
                 }
             },
             {});

    addLabel("player_alive", "Player Dead/Alive",
             []() {
                 auto pl = PlayLayer::get();
                 auto player = pl->m_player1;
                 auto player2 = pl->m_player2;
                 if (pl->m_gameState.m_isDualMode && player2) {
                     return fmt::format("{} / {}",
                                        player->m_isDead ? "Dead" : "Alive",
                                        player2->m_isDead ? "Dead" : "Alive");
                 } else {
                     return fmt::format("{}",
                                        player->m_isDead ? "Dead" : "Alive");
                 }
             },
             {});

    addLabel("bot_state", "Bot State",
             []() {
                 return fmt::format("Bot State: {}", GrapeEngine::get()->isPlaying()
                                                         ? "Playing"
                                                         : "Recording");
             },
             {});
    addLabel("random_state", "Random Seed State",
             []() {
                 return fmt::format(
                     "Random State: {}",
                     GrapeEngine::get()->macro().getCurrentRandomState());
             },
             {});
    addLabel("shake_state", "Random Shake State",
             []() {
                 return fmt::format(
                     "Shake Random State: {}",
                     GrapeEngine::get()->macro().getCurrentShakeState());
             },
             {});
    addLabel("action_index", "Action Index",
             []() {
                 return fmt::format(
                     "Action Index: {}/{}",
                     GrapeEngine::get()->macro().getInputIndex(),
                     GrapeEngine::get()->macro().m_actionAtom.length());
             },
             {});
    addLabel("intentional_death", "Intentional Death",
             []() {
                 if (GrapeEngine::get()->isRecording()) {
                     return fmt::format("Intentional Death: {}",
                                        GrapeEngine::get()->timeline().m_canDie->inner()
                                            ? "Enabled"
                                            : "Disabled");
                 }

                 return fmt::format("Intentional Death: {}",
                                    GrapeEngine::get()->timeline().m_expectsDeath
                                        ? "Expects Death"
                                        : "Nothing");
             },
             {});
    addLabel("ssb_dt", "Scroll Speed Bug DT",
             []() {
                 float tfx = PlayLayer::get()->timeForPos(
                     PlayLayer::get()->m_player1->m_position, 0,
                     PlayLayer::get()->m_gameState.m_currentChannel, true, 0);
                 return fmt::format(
                     "SSB DT: {:.10f}",
                     GrapeEngine::get()->timeline().m_lockDeltaMode->inner() ==
                             FrameEngine::LockDeltaMode::Accuracy
                         ? tfx - GrapeEngine::get()->timeline().m_lastTfp
                         : GrapeEngine::get()->timeline().getPhysicsDt());
             },
             {});

    addLabel("max_upr", "Dynamic UPR",
             []() {
                 if (GrapeEngine::get()->timeline().m_realTime->inner()) {
                     return std::string("Dynamic UPR: Uncapped");
                 }

                 if (GrapeEngine::get()->timeline().m_dynamicUpr->inner()) {
                     return fmt::format("Dynamic UPR: {}",
                                        GrapeEngine::get()->timeline().m_stepLimit);
                 } else {
                     return fmt::format("Static UPR: {}",
                                        GrapeEngine::get()->timeline().m_stepLimit);
                 }
             },
             {});
}

#undef GET_PLAYER_VAR

void LabelManager::update(bool forceDisable) {
    float heights[4] = {0, 0, 0, 0};

    for (auto& label : m_labels) {
        int anchorIndex = static_cast<int>(label.m_config.m_anchor);
        label.update(forceDisable || !this->m_globalEnabled, m_requiresRefresh,
                     heights[anchorIndex]);
    }

    m_requiresRefresh = false;
}

template <>
struct glz::meta<Label::LabelConfig> {
    using T = Label::LabelConfig;
    static constexpr auto value = object(
        "enabled", &T::m_enabled,
        "anchor", &T::m_anchor,
        "font", &T::m_font,
        "opacity", &T::m_opacity,
        "scale", &T::m_scale
    );
};

void LabelManager::readFromConfig() {
    std::unordered_map<std::string, Label::LabelConfig> labels;

    auto labelConfigPath = grape::paths::file("labels.json");
    if (std::filesystem::exists(labelConfigPath)) {
        auto ec = glz::read_file_json(labels, labelConfigPath.string(),
                                      std::string{});
        if (ec) {
            log::error("Failed to read label config: {}",
                       ec.custom_error_message);
            return;
        }
    }

    for (auto& label : m_labels) {
        label.m_config = labels[label.getId()];
    }

    m_requiresRefresh = true;
}

void LabelManager::writeToConfig() {
    std::unordered_map<std::string, Label::LabelConfig> labels;

    for (const auto& label : m_labels) {
        labels[label.getId()] = label.m_config;
    }

    auto labelConfigPath = grape::paths::file("labels.json");

    auto ec = glz::write_file_json<glz::opts{.prettify = true}>(
        labels, labelConfigPath.string(), std::string{});
    if (ec) {
        log::error("Failed to write label config: {}", ec.custom_error_message);
    }
}

CCLabelBMFont* Label::get() {
    auto pl = PlayLayer::get();
    CCLabelBMFont* label = nullptr;

    std::string cocosLabelID =
        fmt::format("{}/label.{}", Mod::get()->getID(), m_id);
    if (pl) {
        if (auto l = pl->getChildByID(cocosLabelID); l) {
            return dynamic_cast<CCLabelBMFont*>(l);
        }

        m_font = resolveFontFile(m_config);

        label = CCLabelBMFont::create(m_display().c_str(), m_font.c_str());

        label->setID(cocosLabelID);
        label->setScale(m_config.m_scale);
        
        PlayLayer::get()->addChild(label, 1000);
    }

    return label;
}

void Label::calculatePosition(float& currentHeight, CCLabelBMFont* label) {
    if (!label) return;

    auto pl = PlayLayer::get();
    const float spacing = 8.0f;
    float innerSpacing = 4.0f * m_config.m_scale;

    switch (m_config.m_anchor) {
        case LabelAnchor::TopLeft:
            m_position = cocos2d::CCPoint(
                spacing, pl->getContentSize().height - spacing - currentHeight);
            m_cocosAnchor = cocos2d::CCPoint(0.0f, 1.0f);
            break;
        case LabelAnchor::TopRight:
            m_position = cocos2d::CCPoint(
                pl->getContentSize().width - spacing,
                pl->getContentSize().height - spacing - currentHeight);
            m_cocosAnchor = cocos2d::CCPoint(1.0f, 1.0f);
            break;
        case LabelAnchor::BottomLeft:
            m_position = cocos2d::CCPoint(10.0f, currentHeight + spacing);
            m_cocosAnchor = cocos2d::CCPoint(0.0f, 0.0f);
            break;
        case LabelAnchor::BottomRight:
            m_position = cocos2d::CCPoint(pl->getContentSize().width - spacing,
                                          currentHeight + spacing);
            m_cocosAnchor = cocos2d::CCPoint(1.0f, 0.0f);
            break;
    }

    currentHeight += label->getScaledContentSize().height + innerSpacing;

    label->setPosition(m_position);
    label->setAnchorPoint(m_cocosAnchor);
}

void Label::update(bool forceDisable, bool refresh, float& currentHeight) {
    auto pl = PlayLayer::get();
    if (pl) {
        CCLabelBMFont* label = get();
        if (!label) return;

        if (!m_config.m_enabled) {
            refresh = false;
            
        }

        if (refresh) {
            m_font = resolveFontFile(m_config);

            label->setFntFile(m_font.c_str());
            label->setOpacity(
                static_cast<GLubyte>(m_config.m_opacity * 255.0f));
            label->setScale(m_config.m_scale);
            this->calculatePosition(currentHeight, label);
        }

        label->setVisible(m_config.m_enabled && !forceDisable);
        label->setString(m_display().c_str());
    }
}

$on_mod(Loaded) { GrapeEngine::get()->labels().readFromConfig(); }

$on_mod(DataSaved) { GrapeEngine::get()->labels().writeToConfig(); }
