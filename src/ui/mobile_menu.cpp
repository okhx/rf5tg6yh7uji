#include "mobile_menu.hpp"

#include <Geode/UI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>
#include <limits>

#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "checkpoint/fix.hpp"
#include "replay/system.hpp"
#include "render/renderer.hpp"
#include "settings/settings.hpp"
#include "trajectory/trajectory.hpp"
#include "util/crash_log.hpp"

using namespace geode::prelude;

namespace {
constexpr float kTabWidth = 84.f;

ButtonSprite* makeButtonSprite(std::string const& text, float width) {
    return ButtonSprite::create(text.c_str(), static_cast<int>(width), true,
                                "bigFont.fnt", "GJ_button_01.png", 24.f,
                                .55f);
}

bool parseNumber(std::string const& text, double& value) {
    if (text.empty() || text == "-" || text == "." || text == "-.") {
        return false;
    }
    try {
        size_t used = 0;
        value = std::stod(text, &used);
        return used == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

ccColor4B toColor(std::array<float, 4> const& value) {
    return {static_cast<GLubyte>(std::clamp(value[0], 0.f, 1.f) * 255.f),
            static_cast<GLubyte>(std::clamp(value[1], 0.f, 1.f) * 255.f),
            static_cast<GLubyte>(std::clamp(value[2], 0.f, 1.f) * 255.f),
            static_cast<GLubyte>(std::clamp(value[3], 0.f, 1.f) * 255.f)};
}

void openColorPicker(std::array<float, 4>* target) {
    auto* popup = geode::ColorPickPopup::create(toColor(*target));
    popup->setCallback([target](ccColor4B const& color) {
        (*target)[0] = color.r / 255.f;
        (*target)[1] = color.g / 255.f;
        (*target)[2] = color.b / 255.f;
        (*target)[3] = color.a / 255.f;
    });
    popup->show();
}

class MobileFeaturePopup final : public geode::Popup {
   public:
    enum class Feature {
        Hitboxes,
        Trajectory,
        Autoclicker,
        Layout,
        Noclip,
        Stepper,
        Rendering,
    };

   private:
    Feature m_feature;
    CCNode* m_content = nullptr;
    CCMenu* m_menu = nullptr;
    int m_category = 0;

    float rowY(int row) const { return 175.f - row * 29.f; }

    void label(std::string const& text, float x, float y, float scale = .34f,
               ccColor3B color = ccWHITE) {
        auto* node = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        node->setScale(scale);
        node->setColor(color);
        node->setPosition({x, y});
        node->limitLabelWidth(270.f, scale, .14f);
        m_content->addChild(node);
    }

    void settingLabel(std::string const& text, float y,
                      float scale = .34f) {
        auto* node = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        node->setAnchorPoint({0.f, .5f});
        node->setScale(scale);
        node->setPosition({28.f, y});
        node->limitLabelWidth(185.f, scale, .14f);
        m_content->addChild(node);
    }

    ButtonSprite* button(std::string const& text, float x, float y,
                         std::function<void()> callback, float width = 85.f) {
        auto* sprite = makeButtonSprite(text, width);
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(
            sprite, [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
                callback();
            });
        item->setPosition({x, y});
        m_menu->addChild(item);
        return sprite;
    }

    void toggle(std::string const& text, int row, bool value,
                std::function<void(bool)> callback, float x = 275.f) {
        const float y = rowY(row);
        settingLabel(text, y);
        auto* item =
            geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
                .42f,
                [callback = std::move(callback)](CCMenuItemToggler* sender) {
                    callback(!sender->isToggled());
                });
        item->toggle(value);
        item->setPosition({x + 75.f, y});
        m_menu->addChild(item);
    }

    TextInput* number(std::string const& text, int row, double value,
                      std::function<void(double)> setter, double minimum,
                      double maximum, int decimals = 2, float x = 260.f) {
        const float y = rowY(row);
        settingLabel(text, y);
        auto* input = TextInput::create(92.f, "Value");
        input->setScaleY(.84f);
        input->setCommonFilter(CommonFilter::Float);
        input->setString(fmt::format("{:.{}f}", value, decimals));
        input->setCallback([setter = std::move(setter), minimum,
                            maximum](std::string const& text) {
            double parsed = 0.0;
            if (parseNumber(text, parsed)) {
                setter(std::clamp(parsed, minimum, maximum));
            }
        });
        input->setPosition({x + 65.f, y});
        m_content->addChild(input, 2);
        return input;
    }

    TextInput* text(std::string const& labelText, int row, std::string value,
                    std::function<void(std::string const&)> setter) {
        const float y = rowY(row);
        settingLabel(labelText, y);
        auto* input = TextInput::create(145.f, "Value");
        input->setScaleY(.84f);
        input->setCommonFilter(CommonFilter::ID);
        input->setString(value);
        input->setCallback(std::move(setter));
        input->setPosition({297.5f, y});
        m_content->addChild(input, 2);
        return input;
    }

    void color(std::string const& text, int row,
               std::array<float, 4>* target) {
        settingLabel(text, rowY(row));
        button("Color", 330.f, rowY(row),
               [target] { openColorPicker(target); }, 80.f);
    }

    void rebuild() {
        m_content->removeAllChildren();
        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        m_content->addChild(m_menu, 3);

        switch (m_feature) {
            case Feature::Hitboxes: buildHitboxes(); break;
            case Feature::Trajectory: buildTrajectory(); break;
            case Feature::Autoclicker: buildAutoclicker(); break;
            case Feature::Layout: buildLayout(); break;
            case Feature::Noclip: buildNoclip(); break;
            case Feature::Stepper: buildStepper(); break;
            case Feature::Rendering: buildRendering(); break;
        }
    }

    void buildHitboxes() {
        auto* settings = SLSettings::get();
        auto* hitboxes = &Bot::get()->hitboxes();
        static constexpr const char* names[] = {
            "Player", "Inner Player", "Rotated Player", "Player Circle",
            "Solid",  "Hazard",       "Passable",       "Interactable",
            "Interactable Active"};
        static constexpr int categories[] = {
            SLSettings::HitboxSettings::Player,
            SLSettings::HitboxSettings::PlayerInner,
            SLSettings::HitboxSettings::PlayerRotated,
            SLSettings::HitboxSettings::PlayerCircle,
            SLSettings::HitboxSettings::Solid,
            SLSettings::HitboxSettings::Hazard,
            SLSettings::HitboxSettings::Passable,
            SLSettings::HitboxSettings::Interactable,
            SLSettings::HitboxSettings::InteractableActive};
        m_category = std::clamp(m_category, 0, 8);
        auto* state = &settings->hitboxes.categories[categories[m_category]];

        number("Line width", 0, hitboxes->m_width->inner(),
               [hitboxes](double value) { hitboxes->m_width->inner() = value; },
               .01, 5.0);
        toggle("Trail", 1, hitboxes->m_trailEnabled->inner(),
               [hitboxes](bool value) {
                   hitboxes->m_trailEnabled->inner() = value;
               });
        toggle("Holding trail", 2, settings->hitboxes.holdingTrailEnabled,
               [settings](bool value) {
                   settings->hitboxes.holdingTrailEnabled = value;
               }, 155.f);
        button("Color", 325.f, rowY(2), [settings] {
            openColorPicker(&settings->hitboxes.holdingTrailColor);
        }, 62.f);
        number("Trail frames", 3, settings->hitboxes.trailMaxLength,
               [settings](double value) {
                   settings->hitboxes.trailMaxLength =
                       std::max(1, static_cast<int>(value));
               },
               1, 100000, 0);
        label(names[m_category], 185.f, rowY(4), .35f, {255, 220, 90});
        button("<", 55.f, rowY(4), [this] {
            m_category = (m_category + 8) % 9;
            rebuild();
        }, 32.f);
        button(">", 315.f, rowY(4), [this] {
            m_category = (m_category + 1) % 9;
            rebuild();
        }, 32.f);
        toggle("Category visible", 5, state->enabled,
               [state](bool value) { state->enabled = value; });
        number("Fill opacity", 6, state->fillOpacity,
               [state](double value) { state->fillOpacity = value; }, 0, 1,
               2, 155.f);
        button("Color", 325.f, rowY(6),
               [state] { openColorPicker(&state->colors); }, 62.f);
    }

    void buildTrajectory() {
        auto* settings = SLSettings::get();
        auto* manager = &Bot::get()->trajectory();
        static constexpr const char* names[] = {
            "Hold",          "Swift",          "Release",
            "Hold Still",    "Swift Still",    "Release Still",
            "Hold Left",     "Swift Left",     "Release Left",
            "Hold Right",    "Swift Right",    "Release Right",
            "Hold Follow",   "Swift Follow",   "Release Follow",
            "Hold Opposite", "Swift Opposite", "Release Opposite"};
        using M = SLSettings::TrajectorySettings::Mode;
        static constexpr int categories[] = {
            M::Hold, M::Swift, M::Release,
            M::Hold | M::Platformer, M::Swift | M::Platformer,
            M::Release | M::Platformer,
            M::Hold | M::Left | M::Platformer,
            M::Swift | M::Left | M::Platformer,
            M::Release | M::Left | M::Platformer,
            M::Hold | M::Right | M::Platformer,
            M::Swift | M::Right | M::Platformer,
            M::Release | M::Right | M::Platformer,
            M::Hold | M::FollowPlayer | M::Platformer,
            M::Swift | M::FollowPlayer | M::Platformer,
            M::Release | M::FollowPlayer | M::Platformer,
            M::Hold | M::FollowOpposite | M::Platformer,
            M::Swift | M::FollowOpposite | M::Platformer,
            M::Release | M::FollowOpposite | M::Platformer};
        m_category = std::clamp(m_category, 0, 17);
        auto* state = &settings->trajectory.categories[categories[m_category]];

        number("Line width", 0, manager->m_state.m_width->inner(),
               [manager](double value) {
                   manager->m_state.m_width->inner() = value;
               },
               .01, 5.0);
        number("Path seconds", 1, manager->m_state.m_length->inner(),
               [manager](double value) {
                   manager->m_state.m_length->inner() = value;
               },
               .05, 30.0);
        number("Max steps (0=auto)", 2, settings->trajectory.maxSteps,
               [settings](double value) {
                   settings->trajectory.maxSteps =
                       std::max(0, static_cast<int>(value));
               },
               0, 1000000, 0);
        label(names[m_category], 185.f, rowY(3), .35f, {255, 220, 90});
        button("<", 55.f, rowY(3), [this] {
            m_category = (m_category + 17) % 18;
            rebuild();
        }, 32.f);
        button(">", 315.f, rowY(3), [this] {
            m_category = (m_category + 1) % 18;
            rebuild();
        }, 32.f);
        toggle("Show category", 4, state->enabled,
               [state](bool value) { state->enabled = value; });
        color("Category color", 5, &state->colors);
    }

    void buildAutoclicker() {
        auto* autoclicker = &Bot::get()->autoclicker();
        static constexpr const char* players[] = {"Player 1", "Player 2",
                                                   "Both"};
        int player = std::clamp(static_cast<int>(autoclicker->m_player), 0, 2);
        settingLabel("Player", rowY(0));
        button(players[player], 310.f, rowY(0), [this, autoclicker] {
            int next = (static_cast<int>(autoclicker->m_player) + 1) % 3;
            autoclicker->m_player =
                static_cast<Autoclicker::PlayerToggle>(next);
            autoclicker->saveSettings();
            rebuild();
        }, 120.f);
        number("Interval frames", 1, autoclicker->m_frequency,
               [autoclicker](double value) {
                   autoclicker->m_frequency =
                       std::max(1, static_cast<int>(value));
                   autoclicker->saveSettings();
               },
               1, 100000, 0);
        toggle("Swift clicks", 2, autoclicker->m_performSwifts,
               [autoclicker](bool value) {
                   autoclicker->m_performSwifts = value;
                   autoclicker->saveSettings();
               });
        label("Autoclicker runs only while recording a macro.", 185.f,
              rowY(4), .3f, {130, 255, 170});
    }

    void buildLayout() {
        auto* settings = SLSettings::get();
        auto* updater = &Bot::get()->updater();
        toggle("Use regular background", 0,
               updater->m_useRegularBg->inner(), [updater](bool value) {
                   updater->m_useRegularBg->inner() = value;
               });
        color("Background color", 2, &settings->layoutBgColor);
        color("Ground color", 3, &settings->layoutGroundColor);
    }

    void buildNoclip() {
        auto* settings = SLSettings::get();
        auto* updater = &Bot::get()->updater();
        static constexpr const char* players[] = {"Player 1", "Player 2",
                                                   "Both"};
        int player = std::clamp(static_cast<int>(updater->m_noclipType), 0, 2);
        settingLabel("Affected player", rowY(0));
        button(players[player], 310.f, rowY(0), [this, updater] {
            int next = (static_cast<int>(updater->m_noclipType) + 1) % 3;
            updater->m_noclipType =
                static_cast<BotUpdater::NoclipType>(next);
            SLSettings::get()->noclipPlayer = next;
            rebuild();
        }, 120.f);
        toggle("Death tint", 1, settings->noclipTintEnabled,
               [settings](bool value) { settings->noclipTintEnabled = value; });
        color("Tint color", 2, &settings->noclipTintColor);
        number("Tint opacity", 3, settings->noclipTintOpacity,
               [settings](double value) {
                   settings->noclipTintOpacity = value;
               },
               0, 1);
        number("Tint time", 4, settings->noclipTintTime,
               [settings](double value) { settings->noclipTintTime = value; },
               0, 10);
    }

    void buildStepper() {
        auto* settings = SLSettings::get();
        auto* updater = &Bot::get()->updater();
        toggle("Backwards stepping", 0,
               updater->m_backwardsStepping->inner(), [updater](bool value) {
                   updater->m_backwardsStepping->inner() = value;
               });
        toggle("Hold arrows", 1, settings->frameStepperHold,
               [settings](bool value) { settings->frameStepperHold = value; });
        number("Hold delay", 2, settings->frameStepperHoldDelay,
               [settings](double value) {
                   settings->frameStepperHoldDelay = value;
               },
               0, 5);
        number("Steps / second", 3, settings->frameStepperHoldSpeed,
               [settings](double value) {
                   settings->frameStepperHoldSpeed = value;
               },
               1, 240);
        number("Saved frames", 4, settings->stepsToSave,
               [settings](double value) {
                   settings->stepsToSave =
                       std::max(2, static_cast<int>(value));
               },
               2, 10000, 0);
    }

    void buildRendering() {
        auto* renderer = Renderer::get();
        number("Width", 0, renderer->m_settings.m_width,
               [renderer](double value) {
                   renderer->m_settings.m_width = static_cast<int>(value);
               },
               16, 16384, 0);
        number("Height", 1, renderer->m_settings.m_height,
               [renderer](double value) {
                   renderer->m_settings.m_height = static_cast<int>(value);
               },
               16, 16384, 0);
        number("FPS", 2, renderer->m_settings.m_fps,
               [renderer](double value) {
                   renderer->m_settings.m_fps = static_cast<int>(value);
               },
               1, 1000, 0);
        number("Bitrate Mbps", 3,
               renderer->m_settings.m_bitrate / 1000000.0,
               [renderer](double value) {
                   renderer->m_settings.m_bitrate =
                       static_cast<uint32_t>(value * 1000000.0);
               },
               1, 1000, 1);
        text("Video codec", 4, renderer->m_settings.m_codec,
             [renderer](std::string const& value) {
                 renderer->m_settings.m_codec = value;
             });
        text("Container", 5, renderer->m_settings.m_extension,
             [renderer](std::string const& value) {
                 renderer->m_settings.m_extension = value;
             });
        button("Save PC preset", 185.f, rowY(6), [renderer] {
            auto path = silicate::paths::directory("presets") /
                        "mobile-render.json";
            renderer->saveSettings(path);
            SLSettings::get()->lastLoadedPreset = "mobile-render";
        }, 145.f);
    }

    bool init(Feature feature) {
        m_feature = feature;
        if (!Popup::init(400.f, 260.f, "GJ_square01.png")) return false;
        static constexpr const char* titles[] = {
            "Hitbox Settings", "Trajectory Settings", "Autoclicker Settings",
            "Layout Settings", "Noclip Settings", "Frame Stepper Settings",
            "Render Settings (PC Preset)"};
        setTitle(titles[static_cast<int>(feature)], "goldFont.fnt", .72f,
                 18.f);
        m_content = CCNode::create();
        m_content->setPosition({15.f, 20.f});
        m_mainLayer->addChild(m_content);
        rebuild();
        return true;
    }

   public:
    static void open(Feature feature) {
        auto* popup = new MobileFeaturePopup();
        if (popup && popup->init(feature)) {
            popup->autorelease();
            popup->show();
        } else {
            CC_SAFE_DELETE(popup);
        }
    }
};

class MobileMacroEditorPopup final : public geode::Popup {
    CCNode* m_content = nullptr;
    CCMenu* m_menu = nullptr;
    int m_selected = 0;
    CCLabelBMFont* m_inputLabel = nullptr;

    slc::ActionAtom& atom() {
        return Bot::get()->replaySystem().m_actionAtom;
    }

    int currentInputIndex() {
        auto* bot = Bot::get();
        const auto size = atom().m_actions.size();
        if (size == 0) return -1;
        if (bot->isRecording()) return static_cast<int>(size - 1);

        const size_t next = bot->replaySystem().m_inputIndex;
        return next == 0
            ? -1
            : static_cast<int>(std::min(next - 1, size - 1));
    }

    void normalize() {
        auto& replay = Bot::get()->replaySystem();
        auto& inputs = atom().m_actions;
        std::stable_sort(inputs.begin(), inputs.end(), [](const auto& a,
                                                          const auto& b) {
            return a.m_frame < b.m_frame;
        });
        uint64_t previous = 0;
        for (auto& input : inputs) {
            input.recalculateDelta(previous);
            previous = input.m_frame;
        }
        if (Bot::get()->isPlaying()) {
            replay.seekAfterFrame(Bot::get()->updater().getFrame());
        } else if (Bot::get()->isRecording()) {
            replay.m_inputIndex = inputs.size();
        } else {
            replay.m_inputIndex = 0;
        }
        if (inputs.empty()) {
            m_selected = 0;
        } else {
            m_selected = std::clamp(
                m_selected, 0, static_cast<int>(inputs.size()) - 1);
        }
    }

    CCLabelBMFont* label(std::string const& text, float x, float y,
                         float scale = .34f, ccColor3B color = ccWHITE) {
        auto* node = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        node->setAnchorPoint({0.f, .5f});
        node->setScale(scale);
        node->setColor(color);
        node->setPosition({x, y});
        node->limitLabelWidth(250.f, scale, .14f);
        m_content->addChild(node);
        return node;
    }

    void button(std::string const& text, float x, float y,
                std::function<void()> callback, float width = 82.f) {
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(
            makeButtonSprite(text, width),
            [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
                callback();
            });
        item->setPosition({x, y});
        m_menu->addChild(item);
    }

    void toggle(std::string const& text, float x, float y, bool value,
                std::function<void(bool)> callback) {
        label(text, x, y);
        auto* item =
            geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
                .4f, [callback = std::move(callback)](CCMenuItemToggler* item) {
                    callback(!item->isToggled());
                });
        item->toggle(value);
        item->setPosition({x + 145.f, y});
        m_menu->addChild(item);
    }

    void rebuild() {
        m_inputLabel = nullptr;
        m_content->removeAllChildren();
        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        m_content->addChild(m_menu, 3);

        auto& inputs = atom().m_actions;
        if (inputs.empty()) {
            label("No macro inputs", 24.f, 170.f, .4f, {255, 220, 90});
            button("Add input", 190.f, 105.f, [this] {
                atom().m_actions.emplace_back(
                    0, 0, slc::Action::ActionType::Jump, true, false);
                normalize();
                rebuild();
            }, 120.f);
            return;
        }

        m_selected = std::clamp(
            m_selected, 0, static_cast<int>(inputs.size()) - 1);
        auto* input = &inputs[m_selected];
        auto* bot = Bot::get();
        const int currentInput = currentInputIndex();
        const bool isCurrent = (bot->isRecording() || bot->isPlaying()) &&
                               m_selected == currentInput;
        m_inputLabel = label(
            fmt::format("Input {} / {}", m_selected + 1, inputs.size()),
            24.f, 188.f, .38f,
            isCurrent ? ccColor3B{255, 125, 125}
                      : ccColor3B{255, 220, 90});
        button("<", 285.f, 188.f, [this] {
            const int size = static_cast<int>(atom().m_actions.size());
            m_selected = (m_selected + size - 1) % size;
            rebuild();
        }, 34.f);
        button(">", 335.f, 188.f, [this] {
            m_selected = (m_selected + 1) % atom().m_actions.size();
            rebuild();
        }, 34.f);

        label("Frame", 24.f, 153.f);
        auto* frame = TextInput::create(105.f, "Frame");
        frame->setScaleY(.84f);
        frame->setCommonFilter(CommonFilter::Int);
        frame->setString(std::to_string(input->m_frame));
        frame->setCallback([this](std::string const& value) {
            double parsed = 0.0;
            if (!parseNumber(value, parsed) || atom().m_actions.empty()) return;
            const uint64_t target = static_cast<uint64_t>(std::clamp(
                parsed, 0.0,
                static_cast<double>(std::numeric_limits<uint32_t>::max())));
            atom().m_actions[m_selected].m_frame = target;
        });
        frame->setPosition({190.f, 153.f});
        m_content->addChild(frame, 2);

        toggle("Holding / click", 24.f, 118.f, input->m_holding,
               [this](bool value) {
                   if (!atom().m_actions.empty())
                       atom().m_actions[m_selected].m_holding = value;
               });
        toggle("Player 2", 210.f, 118.f, input->m_player2,
               [this](bool value) {
                   if (!atom().m_actions.empty())
                       atom().m_actions[m_selected].m_player2 = value;
               });

        button("Add", 105.f, 88.f, [this] {
            auto& inputs = atom().m_actions;
            const auto& selected = inputs[m_selected];
            inputs.emplace_back(selected.m_frame + 1, 0,
                                slc::Action::ActionType::Jump,
                                !selected.m_holding, selected.m_player2);
            ++m_selected;
            normalize();
            rebuild();
        }, 72.f);
        button("Delete", 265.f, 88.f, [this] {
            auto& inputs = atom().m_actions;
            inputs.erase(inputs.begin() + m_selected);
            normalize();
            rebuild();
        }, 82.f);
        button("Remove P1", 105.f, 55.f, [this] {
            std::erase_if(atom().m_actions,
                          [](const auto& action) { return !action.m_player2; });
            normalize();
            rebuild();
        }, 96.f);
        button("Remove P2", 265.f, 55.f, [this] {
            std::erase_if(atom().m_actions,
                          [](const auto& action) { return action.m_player2; });
            normalize();
            rebuild();
        }, 96.f);

        button("Flip hold", 105.f, 22.f, [this] {
            for (auto& action : atom().m_actions)
                action.m_holding = !action.m_holding;
            rebuild();
        }, 115.f);
        button("Flip players", 265.f, 22.f, [this] {
            for (auto& action : atom().m_actions)
                action.m_player2 = !action.m_player2;
            rebuild();
        }, 125.f);
    }

    bool init() override {
        if (!Popup::init(420.f, 270.f, "GJ_square01.png")) return false;
        setTitle("Macro Editor", "goldFont.fnt", .72f, 18.f);
        m_content = CCNode::create();
        m_content->setPosition({15.f, 20.f});
        m_mainLayer->addChild(m_content);

        const auto& inputs = atom().m_actions;
        if (!inputs.empty()) {
            m_selected = std::max(0, currentInputIndex());
        }
        this->scheduleUpdate();
        rebuild();
        return true;
    }

    void update(float) override {
        auto* bot = Bot::get();
        if ((!bot->isRecording() && !bot->isPlaying()) ||
            atom().m_actions.empty()) {
            if (m_inputLabel) m_inputLabel->setColor({255, 220, 90});
            return;
        }

        const int active = currentInputIndex();
        if (m_inputLabel) {
            m_inputLabel->setColor(
                m_selected == active ? ccColor3B{255, 125, 125}
                                     : ccColor3B{255, 220, 90});
        }
    }

    void onClose(CCObject* sender) override {
        normalize();
        Popup::onClose(sender);
    }

   public:
    static void open() {
        auto* popup = new MobileMacroEditorPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            popup->show();
        } else {
            CC_SAFE_DELETE(popup);
        }
    }
};
}

bool MobileMenu::init() {
    const auto winSize = CCDirector::get()->getWinSize();
    const float width = std::min(470.f, winSize.width - 24.f);
    const float height = std::min(290.f, winSize.height - 18.f);
    if (!Popup::init(width, height, "GJ_square01.png")) return false;

    this->setTitle("Grape Mobile", "goldFont.fnt", .75f, 18.f);

    static constexpr const char* names[] = {
        "Record", "Assist", "Settings", "Render"};
    for (int i = 0; i < 4; ++i) {
        auto sprite = makeButtonSprite(names[i], kTabWidth);
        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(MobileMenu::onTab));
        button->setTag(i);
        button->setPosition({m_size.width / 2.f + (i - 1.5f) * 94.f,
                             m_size.height - 48.f});
        m_buttonMenu->addChild(button);
    }

    m_pageNode = CCNode::create();
    m_pageNode->setAnchorPoint({0.f, 0.f});
    m_pageNode->setPosition({14.f, 28.f});
    m_pageNode->setContentSize({m_size.width - 28.f, m_size.height - 86.f});
    m_mainLayer->addChild(m_pageNode);

    this->scheduleUpdate();
    rebuildPage();
    return true;
}

void MobileMenu::update(float) {
    auto* bot = Bot::get();
    if (m_recordSprite) {
        m_recordSprite->setString(bot->isRecording() ? "Stop recording"
                                                     : "Start recording");
        m_recordSprite->setColor(bot->isRecording()
            ? ccColor3B{210, 85, 85} : ccWHITE);
    }
    if (m_playSprite) {
        m_playSprite->setString(bot->isPlaying() ? "Stop playback"
                                                 : "Start playback");
        m_playSprite->setColor(bot->isPlaying()
            ? ccColor3B{105, 190, 245} : ccWHITE);
    }
    if (m_renderSprite) {
        m_renderSprite->setString(Renderer::get()->isRecording()
                                      ? "Stop render" : "Start render");
    }
    if (m_frameLabel) {
        m_frameLabel->setString(
            fmt::format("Frame {}  |  {} inputs", bot->updater().getDisplayFrame(),
                        bot->replaySystem().m_actionAtom.length())
                .c_str());
    }
    if (m_statusLabel) m_statusLabel->setString(m_status.c_str());
}

float MobileMenu::columnX(int column) const {
    return m_pageNode->getContentSize().width * (column == 0 ? .25f : .75f);
}

float MobileMenu::rowY(int row) const {
    return m_pageNode->getContentSize().height - 15.f - row * 25.f;
}

void MobileMenu::addLabel(std::string const& text, float x, float y,
                          float scale, ccColor3B color) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    label->setColor(color);
    label->setPosition({x, y});
    label->limitLabelWidth(185.f, scale, .15f);
    m_pageNode->addChild(label);
}

void MobileMenu::addFunctionLabel(std::string const& text, float x, float y) {
    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setAnchorPoint({0.f, .5f});
    label->setScale(.33f);
    label->setPosition({x, y});
    label->limitLabelWidth(92.f, .33f, .15f);
    m_pageNode->addChild(label);
}

ButtonSprite* MobileMenu::addButton(std::string const& text, float x, float y,
                                    std::function<void()> callback,
                                    float width) {
    auto sprite = makeButtonSprite(text, width);
    auto button = geode::cocos::CCMenuItemExt::createSpriteExtra(
        sprite, [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
            callback();
        });
    button->setPosition({x, y});
    m_pageMenu->addChild(button);
    return sprite;
}

void MobileMenu::addToggle(std::string const& text, int row, int column,
                           bool value, std::function<void(bool)> callback) {
    const float x = columnX(column);
    const float y = rowY(row);
    addFunctionLabel(text, x - 94.f, y);

    auto toggle = geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
        .42f, [callback = std::move(callback)](CCMenuItemToggler* item) {
            callback(!item->isToggled());
        });
    toggle->toggle(value);
    toggle->setPosition({x + 72.f, y});
    m_pageMenu->addChild(toggle);
}

void MobileMenu::addToggleWithSettings(
    std::string const& text, int row, int column, bool value,
    std::function<void(bool)> callback, std::function<void()> openSettings) {
    const float x = columnX(column);
    const float y = rowY(row);
    addFunctionLabel(text, x - 94.f, y);

    auto* toggle =
        geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
            .38f, [callback = std::move(callback)](CCMenuItemToggler* item) {
                callback(!item->isToggled());
            });
    toggle->toggle(value);
    toggle->setPosition({x + 55.f, y});
    m_pageMenu->addChild(toggle);
    addButton("+", x + 88.f, y, std::move(openSettings), 19.f);
}

TextInput* MobileMenu::addNumberInput(
    std::string const& text, int row, int column,
    std::function<double()> getter, std::function<void(double)> setter,
    double minimum, double maximum, int decimals, float width) {
    const float x = columnX(column);
    const float y = rowY(row);
    addLabel(text, x - 55.f, y, .32f);
    auto* input = TextInput::create(width, "Value");
    input->setScaleY(.84f);
    input->setCommonFilter(CommonFilter::Float);
    input->setString(fmt::format("{:.{}f}", getter(), decimals));
    input->setCallback([setter = std::move(setter), minimum,
                        maximum](std::string const& text) {
        double parsed = 0.0;
        if (parseNumber(text, parsed)) {
            setter(std::clamp(parsed, minimum, maximum));
        }
    });
    input->setPosition({x + 47.f, y});
    m_pageNode->addChild(input, 3);
    return input;
}

void MobileMenu::addAdjuster(
    std::string const& text, int row, int column,
    std::function<double()> getter, std::function<void(double)> setter,
    double step, double minimum, double maximum,
    std::function<std::string(double)> formatter) {
    const float x = columnX(column);
    const float y = rowY(row);
    addLabel(text, x - 57.f, y, .32f);

    auto* input = TextInput::create(60.f, "Value");
    input->setScaleY(.84f);
    input->setCommonFilter(CommonFilter::Float);
    input->setString(formatter(getter()));
    input->setPosition({x + 42.f, y});
    m_pageNode->addChild(input, 3);

    const auto apply = [setter, formatter, input, minimum,
                        maximum](double value) {
        value = std::clamp(value, minimum, maximum);
        setter(value);
        input->setString(formatter(value), false);
    };
    input->setCallback([apply](std::string const& text) {
        double value = 0.0;
        if (parseNumber(text, value)) apply(value);
    });
    const auto change = [getter, apply, minimum,
                         maximum](double delta) {
        const double value = std::clamp(getter() + delta, minimum, maximum);
        apply(value);
    };
    addButton("-", x - 1.f, y, [change, step] { change(-step); }, 16.f);
    addButton("+", x + 84.f, y, [change, step] { change(step); }, 16.f);
}

void MobileMenu::rebuildPage() {
    m_frameLabel = nullptr;
    m_statusLabel = nullptr;
    m_recordSprite = nullptr;
    m_playSprite = nullptr;
    m_renderSprite = nullptr;
    m_pageNode->removeAllChildren();
    m_pageMenu = CCMenu::create();
    m_pageMenu->setPosition({0.f, 0.f});
    m_pageMenu->setContentSize(m_pageNode->getContentSize());
    m_pageNode->addChild(m_pageMenu, 2);

    switch (m_page) {
        case Page::Record: buildRecordPage(); break;
        case Page::Assist: buildAssistPage(); break;
        case Page::Settings: buildSettingsPage(); break;
        case Page::Render: buildRenderPage(); break;
    }
}

void MobileMenu::buildRecordPage() {
    auto* bot = Bot::get();
    auto& updater = bot->updater();
    auto& replay = bot->replaySystem();

    m_frameLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_frameLabel->setScale(.36f);
    m_frameLabel->setColor({255, 220, 90});
    m_frameLabel->setPosition(
        {m_pageNode->getContentSize().width / 2.f, rowY(0)});
    m_pageNode->addChild(m_frameLabel);

    m_recordSprite = addButton(
        bot->isRecording() ? "Stop recording" : "Start recording",
        columnX(0), rowY(1), [this, bot, &replay, &updater] {
            if (bot->isRecording()) {
                bot->setMode(Bot::Stopped);
            } else {
                if (!replay.m_actionAtom.m_actions.empty()) {
                    replay.createBackup();
                }
                bot->setMode(Bot::Recording);
                replay.m_inputIndex = 0;
                if (auto* playLayer = PlayLayer::get()) {
                    playLayer->resetLevel();
                } else {
                    replay.m_actionAtom.clipActions(0);
                    updater.resetFrame();
                }
            }
            m_recordSprite->setString(bot->isRecording() ? "Stop recording"
                                                         : "Start recording");
            m_recordSprite->setColor(bot->isRecording()
                ? ccColor3B{210, 85, 85} : ccWHITE);
        }, 150.f);

    m_playSprite = addButton(
        bot->isPlaying() ? "Stop playback" : "Start playback", columnX(1),
        rowY(1), [this, bot, &replay] {
            if (bot->isPlaying()) {
                bot->setMode(Bot::Stopped);
            } else {
                if (auto* playLayer = PlayLayer::get(); playLayer &&
                    (playLayer->m_hasCompletedLevel ||
                     playLayer->m_levelEndAnimationStarted)) {
                    m_status = "Close the completion screen before playback";
                    return;
                }
                bot->setMode(Bot::Playing);
                replay.m_inputIndex = 0;
                if (auto* playLayer = PlayLayer::get()) {
                    playLayer->resetLevel();
                }
            }
            m_playSprite->setString(bot->isPlaying() ? "Stop playback"
                                                     : "Start playback");
            m_playSprite->setColor(bot->isPlaying()
                ? ccColor3B{105, 190, 245} : ccWHITE);
        }, 150.f);

    addNumberInput(
        "TPS", 2, 0, [&updater] { return updater.m_tps->inner(); },
        [&](double value) {
            updater.m_tps->inner() = value;
            updater.m_tps->notifyChange();
        },
        1.0, 10000.0, 0);
    addAdjuster(
        "Speed", 2, 1, [&] { return updater.m_speedhack->inner(); },
        [&](double value) {
            updater.m_speedhack->inner() = value;
            updater.m_speedhack->notifyChange();
        },
        .05, .05, 100.0,
        [](double value) { return fmt::format("{:.2f}", value); });

    addToggle("Audio speedhack", 3, 0, updater.m_speedhackAudio->inner(),
              [&](bool value) {
                  updater.m_speedhackAudio->inner() = value;
                  updater.m_speedhackAudio->notifyChange();
              });
    addToggle("Ignore play inputs", 3, 1, replay.m_ignoreInputs->inner(),
              [&](bool value) { replay.m_ignoreInputs->inner() = value; });

    auto input = geode::TextInput::create(185.f, "Replay name");
    input->setScaleY(.84f);
    input->setCommonFilter(geode::CommonFilter::Name);
    input->setMaxCharCount(64);
    input->setString(replay.m_replayName);
    input->setCallback([&replay](std::string const& value) {
        replay.m_replayName = value;
    });
    input->setPosition({m_pageNode->getContentSize().width * .30f, rowY(4)});
    m_pageNode->addChild(input, 1);

    addButton("Save", m_pageNode->getContentSize().width * .67f, rowY(4),
              [this, &replay] {
                  if (replay.m_replayName.empty())
                      replay.m_replayName = "mobile-replay";
                  auto path = replay.getCurrentPath();
                  replay.backupExisting(path);
                  replay.save(path);
                  m_status = replay.m_lastOperationSucceeded
                                 ? fmt::format("Macro saved [{} inputs]",
                                               replay.m_actionAtom.length())
                                 : "Macro save failed";
              }, 70.f);
    addButton("Load", m_pageNode->getContentSize().width * .86f, rowY(4),
              [this, &replay] {
                  if (replay.m_replayName.empty()) return;
                  auto* playLayer = PlayLayer::get();
                  const bool completionOpen =
                      playLayer && (playLayer->m_hasCompletedLevel ||
                                    playLayer->m_levelEndAnimationStarted);
                  auto path = replay.getCurrentPath();
                  if (!std::filesystem::exists(path)) {
                      path.replace_extension(".slc");
                  }
                  if (std::filesystem::exists(path)) {
                      replay.load(path);
                      if (replay.m_lastOperationSucceeded) {
                          if (completionOpen) {
                              Bot::get()->setMode(Bot::Stopped);
                              replay.m_inputIndex = 0;
                          } else if (playLayer) {
                              playLayer->resetLevel();
                          }
                          m_status = completionOpen
                              ? fmt::format(
                                    "Macro loaded [{} inputs] - close result",
                                    replay.m_actionAtom.length())
                              : fmt::format("Macro loaded [{} inputs]",
                                            replay.m_actionAtom.length());
                      } else {
                          m_status = "Macro load failed";
                      }
                  } else {
                      m_status = "Macro not found";
                  }
              }, 70.f);

    addToggleWithSettings(
        "Frame stepper", 5, 0, updater.m_paused->inner(),
        [&updater](bool value) {
            updater.setPaused(value);
            updater.m_backwardsStepping->inner() = value;
        },
        [] {
            MobileFeaturePopup::open(MobileFeaturePopup::Feature::Stepper);
        });
    addToggle("Intentional death", 5, 1, updater.m_canDie->inner(),
              [&](bool value) { updater.m_canDie->inner() = value; });
    addToggle("Frame extrapolation", 6, 0,
              updater.m_extrapolateFrames->inner(), [&](bool value) {
                  updater.m_extrapolateFrames->inner() = value;
              });
    addButton("Macro editor", columnX(1), rowY(6),
              [] { MobileMacroEditorPopup::open(); }, 130.f);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(.27f);
    m_statusLabel->setColor({130, 255, 170});
    m_statusLabel->setPosition(
        {m_pageNode->getContentSize().width / 2.f, 2.f});
    m_pageNode->addChild(m_statusLabel);
}

void MobileMenu::buildAssistPage() {
    auto* bot = Bot::get();
    auto& updater = bot->updater();
    auto& hitboxes = bot->hitboxes();
    auto& trajectory = bot->trajectory();

    addToggleWithSettings(
        "Show hitboxes", 0, 0, hitboxes.m_enabled->inner(),
        [&hitboxes](bool value) { hitboxes.m_enabled->inner() = value; },
        [] {
            MobileFeaturePopup::open(MobileFeaturePopup::Feature::Hitboxes);
        });
    addToggle("Hitbox trail", 0, 1, hitboxes.m_trailEnabled->inner(),
              [&](bool value) { hitboxes.m_trailEnabled->inner() = value; });
    addToggleWithSettings(
        "Noclip", 1, 0, updater.m_noclip->inner(),
        [&updater](bool value) { updater.m_noclip->inner() = value; }, [] {
            MobileFeaturePopup::open(MobileFeaturePopup::Feature::Noclip);
        });
    addToggleWithSettings(
        "Trajectory", 1, 1, trajectory.m_state.m_enabled->inner(),
        [&trajectory](bool value) { trajectory.setEnabled(value); }, [] {
            MobileFeaturePopup::open(MobileFeaturePopup::Feature::Trajectory);
        });
    addToggleWithSettings(
        "Layout mode", 2, 0, updater.m_layoutMode->inner(),
        [&updater](bool value) {
                  updater.m_layoutMode->inner() = value;
                  updater.m_layoutMode->notifyChange();
        },
        [] {
            MobileFeaturePopup::open(MobileFeaturePopup::Feature::Layout);
        });
    auto* autoclicker = &bot->autoclicker();
    addToggleWithSettings(
        "Autoclicker", 2, 1, autoclicker->m_enabled->inner(),
        [autoclicker](bool value) {
            autoclicker->m_enabled->inner() = value;
            crash_log::breadcrumb(fmt::format(
                "Autoclicker {} (interval={}, player={}, swifts={})",
                value ? "enabled" : "disabled", autoclicker->m_frequency,
                static_cast<int>(autoclicker->m_player),
                autoclicker->m_performSwifts));
        },
        [] {
            MobileFeaturePopup::open(
                MobileFeaturePopup::Feature::Autoclicker);
        });
    addToggle("No mirror", 3, 0, updater.m_noMirror->inner(),
              [&](bool value) { updater.m_noMirror->inner() = value; });

    addAdjuster(
        "Hitbox width", 4, 0, [&] { return hitboxes.m_width->inner(); },
        [&](double value) { hitboxes.m_width->inner() = value; }, .05, 0.05,
        1.0, [](double value) { return fmt::format("{:.2f}", value); });
    addAdjuster(
        "Path length", 4, 1,
        [&] { return trajectory.m_state.m_length->inner(); },
        [&](double value) { trajectory.m_state.m_length->inner() = value; },
        .1, .1, 5.0,
        [](double value) { return fmt::format("{:.1f}", value); });
    addToggle("SSB fix", 5, 0, updater.m_ssbFix->inner(),
              [&](bool value) { updater.m_ssbFix->inner() = value; });
    addToggle("Use regular updates", 5, 1,
              updater.m_useVisualUpdates->inner(), [&](bool value) {
                  updater.m_useVisualUpdates->inner() = value;
              });
}

void MobileMenu::buildSettingsPage() {
    auto* bot = Bot::get();
    auto& updater = bot->updater();
    auto& replay = bot->replaySystem();

    addToggle("Bot enabled", 0, 0, bot->m_enabled->inner(), [bot](bool value) {
        bot->m_enabled->inner() = value;
        bot->m_enabled->notifyChange();
    });
    addToggle("Lock delta", 0, 1, updater.m_lockDelta->inner(),
              [&](bool value) { updater.m_lockDelta->inner() = value; });
    addToggle("Real time", 1, 0, updater.m_realTime->inner(),
              [&](bool value) { updater.m_realTime->inner() = value; });
    addToggle("Dynamic UPR", 1, 1, updater.m_dynamicUpr->inner(),
              [&](bool value) { updater.m_dynamicUpr->inner() = value; });
    addToggle("Autosave at end", 2, 0,
              replay.m_autosaveAtLevelEnd->inner(), [&](bool value) {
                  replay.m_autosaveAtLevelEnd->inner() = value;
              });
    addToggle("Periodic backup", 2, 1,
              replay.m_autosaveAtInterval->inner(), [&](bool value) {
                  replay.m_autosaveAtInterval->inner() = value;
                  replay.m_autosaveAtInterval->notifyChange();
              });
    addToggle("Visual updates", 3, 0,
              updater.m_useVisualUpdates->inner(), [&](bool value) {
                  updater.m_useVisualUpdates->inner() = value;
              });

    addToggle("End screen menu", 3, 1,
              SLSettings::get()->showEndMenuButton, [](bool value) {
                  SLSettings::get()->showEndMenuButton = value;
              });

    addNumberInput(
        "Backup seconds", 4, 0,
        [&] { return replay.m_autosaveInterval->inner(); },
        [&](double value) {
            replay.m_autosaveInterval->inner() = value;
            replay.m_autosaveInterval->notifyChange();
        },
        15.0, 3600.0, 0);
    addNumberInput(
        "Target FPS", 4, 1, [&] { return updater.m_fpsTarget->inner(); },
        [&](double value) { updater.m_fpsTarget->inner() = value; }, 15.0,
        480.0, 0);

}

void MobileMenu::buildRenderPage() {
    auto* renderer = Renderer::get();
    auto& settings = renderer->m_settings;
    const float center = m_pageNode->getContentSize().width / 2.f;

    const auto leftLabel = [this](std::string const& text, float x, float y,
                                  float width = 130.f) {
        auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setAnchorPoint({0.f, .5f});
        label->setScale(.32f);
        label->setPosition({x, y});
        label->limitLabelWidth(width, .32f, .14f);
        m_pageNode->addChild(label);
    };

    leftLabel("Resolution", 18.f, rowY(0));
#ifdef GEODE_IS_IOS
    m_renderResolutionIndex = iosRenderResolutionIndex(
        settings.m_width, settings.m_height);
    const auto applyResolution = [this, renderer] {
        const auto& resolution =
            IOS_RENDER_RESOLUTIONS[m_renderResolutionIndex];
        renderer->m_settings.m_width = resolution.m_width;
        renderer->m_settings.m_height = resolution.m_height;
    };
    applyResolution();
    const auto resolutionText = [this] {
        const auto& resolution =
            IOS_RENDER_RESOLUTIONS[m_renderResolutionIndex];
        return fmt::format("{}  {}x{}", resolution.m_name,
                           resolution.m_width, resolution.m_height);
    };
    auto* resolutionLabel = CCLabelBMFont::create(
        resolutionText().c_str(), "bigFont.fnt");
    resolutionLabel->setAnchorPoint({0.f, .5f});
    resolutionLabel->setScale(.31f);
    resolutionLabel->setPosition({247.f, rowY(0)});
    resolutionLabel->limitLabelWidth(145.f, .31f, .14f);
    m_pageNode->addChild(resolutionLabel);
    addButton("<", 225.f, rowY(0),
              [this, applyResolution, resolutionLabel, resolutionText] {
                  m_renderResolutionIndex =
                      (m_renderResolutionIndex +
                       IOS_RENDER_RESOLUTIONS.size() - 1) %
                      IOS_RENDER_RESOLUTIONS.size();
                  applyResolution();
                  resolutionLabel->setString(resolutionText().c_str());
              }, 24.f);
    addButton(">", 405.f, rowY(0),
              [this, applyResolution, resolutionLabel, resolutionText] {
                  m_renderResolutionIndex =
                      (m_renderResolutionIndex + 1) %
                      IOS_RENDER_RESOLUTIONS.size();
                  applyResolution();
                  resolutionLabel->setString(resolutionText().c_str());
              }, 24.f);
#else
    auto makeNumber = [this](float x, float y, int value,
                             std::function<void(int)> setter) {
        auto* input = TextInput::create(72.f, "Value");
        input->setScaleY(.84f);
        input->setCommonFilter(CommonFilter::Int);
        input->setString(std::to_string(value));
        input->setCallback([setter = std::move(setter)](std::string const& s) {
            double value = 0.0;
            if (parseNumber(s, value)) setter(static_cast<int>(value));
        });
        input->setPosition({x, y});
        m_pageNode->addChild(input, 3);
    };
    makeNumber(145.f, rowY(0), settings.m_width,
               [&settings](int value) {
                   settings.m_width = std::clamp(value, 64, 3840);
               });
    leftLabel("x", 188.f, rowY(0), 20.f);
    makeNumber(245.f, rowY(0), settings.m_height,
               [&settings](int value) {
                   settings.m_height = std::clamp(value, 64, 2160);
               });
#endif

    auto makeValue = [this, &leftLabel](std::string const& title, int row,
                                        float labelX, float inputX,
                                        double value,
                                        std::function<void(double)> setter,
                                        int decimals = 0) {
        leftLabel(title, labelX, rowY(row));
        auto* input = TextInput::create(72.f, "Value");
        input->setScaleY(.84f);
        input->setCommonFilter(CommonFilter::Float);
        input->setString(fmt::format("{:.{}f}", value, decimals));
        input->setCallback([setter = std::move(setter)](std::string const& s) {
            double parsed = 0.0;
            if (parseNumber(s, parsed)) setter(parsed);
        });
        input->setPosition({inputX, rowY(row)});
        m_pageNode->addChild(input, 3);
    };
#ifdef GEODE_IS_IOS
    settings.m_fps = 240;
    leftLabel("FPS", 18.f, rowY(1));
    auto* fpsInput = TextInput::create(72.f, "Value");
    fpsInput->setScaleY(.84f);
    fpsInput->setString("240");
    fpsInput->setEnabled(false);
    fpsInput->setPosition({145.f, rowY(1)});
    m_pageNode->addChild(fpsInput, 3);
#else
    makeValue("FPS", 1, 18.f, 145.f, settings.m_fps,
              [&settings](double value) {
                  settings.m_fps = std::clamp(static_cast<int>(value), 1, 240);
              });
#endif
    makeValue("Bitrate Mbps", 1, 225.f, 365.f,
              settings.m_bitrate / 1'000'000.0,
              [&settings](double value) {
                  settings.m_bitrate = static_cast<uint32_t>(
                      std::clamp(value, 1.0, 200.0) * 1'000'000.0);
              });

#ifndef GEODE_IS_IOS
    auto makeText = [this](std::string const& title, int row, int column,
                           std::string value,
                           std::function<void(std::string const&)> setter) {
        const float labelX = column == 0 ? 18.f : 225.f;
        const float inputX = column == 0 ? 145.f : 365.f;
        auto* label = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
        label->setAnchorPoint({0.f, .5f});
        label->setScale(.32f);
        label->setPosition({labelX, rowY(row)});
        label->limitLabelWidth(130.f, .32f, .14f);
        m_pageNode->addChild(label);
        auto* input = TextInput::create(105.f, "Value");
        input->setScaleY(.84f);
        input->setString(value);
        input->setCallback(std::move(setter));
        input->setPosition({inputX, rowY(row)});
        m_pageNode->addChild(input, 3);
    };
    makeText("Codec", 2, 0, settings.m_codec,
             [&settings](std::string const& value) {
                 settings.m_codec = value;
             });
    makeText("Container", 2, 1, settings.m_extension,
             [&settings](std::string const& value) {
                 settings.m_extension = value;
             });
#else
    leftLabel("Encoder: H.264 (native)", 18.f, rowY(2), 180.f);
    leftLabel("Container: MP4", 225.f, rowY(2), 150.f);
#endif
    makeValue("After end", 3, 18.f, 145.f, settings.m_afterEndTime,
              [&settings](double value) {
                  settings.m_afterEndTime = static_cast<float>(
                      std::clamp(value, 0.0, 30.0));
              }, 1);
    addToggle("Level complete", 3, 1, settings.m_showLevelComplete,
              [&settings](bool value) {
                  settings.m_showLevelComplete = value;
              });

    addToggle("Level only", 4, 0, settings.m_renderOnlyLevel,
              [&settings](bool value) {
                  settings.m_renderOnlyLevel = value;
              });
    addToggle("Audio", 4, 1, settings.m_renderAudio,
              [&settings](bool value) { settings.m_renderAudio = value; });

    m_renderSprite = addButton(
        renderer->isRecording() ? "Stop render" : "Start render",
        center, rowY(5), [this, renderer] {
            if (renderer->isRecording()) {
                renderer->signalStop();
                m_status = "Render saved";
                return;
            }
            auto result = renderer->start();
            m_status = result.isOk()
                ? "Renderer ready - start or restart the level"
                                     : result.unwrapErr();
        }, 155.f);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setAnchorPoint({0.f, .5f});
    m_statusLabel->setScale(.27f);
    m_statusLabel->setColor({130, 255, 170});
    m_statusLabel->setPosition({18.f, rowY(6)});
    m_statusLabel->limitLabelWidth(390.f, .27f, .16f);
    m_pageNode->addChild(m_statusLabel);
}

void MobileMenu::onTab(CCObject* sender) {
    m_page = static_cast<Page>(sender->getTag());
    rebuildPage();
}

MobileMenu* MobileMenu::create() {
    auto* ret = new MobileMenu();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void MobileMenu::open() {
    if (auto* popup = create()) popup->show();
}
