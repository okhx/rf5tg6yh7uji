#pragma once
#include "util/paths.hpp"

#include <Geode/Geode.hpp>
#include <fstream>
#include <glaze/glaze.hpp>
#include <string>
#include <unordered_map>

#include "../shared/value/value.hpp"

class SLSettings {
   public:
    bool botEnabled = true;

    bool uiVisible = false;

    bool glassUi = true;
    float animationSpeed = 1.0f;
    bool playAnimations = true;
    float uiScale = 1.0f;
    float uiOpacity = 0.66f;
    bool fitMenuToContent = false;
    int theme = 0;

    bool autosaveAtLevelEnd = true;
    bool autosaveAtInterval = true;
    double autosaveInterval = 180.0;

    bool rainbowMode = true;

    double tps = 240.0;
    double speed = 1.0;

    bool realTime = true;
    uint32_t maxUpr = 10;
    bool useVisualUpdates = true;
    bool dynamicUpr = true;
    double fpsTarget = 60.0;
    bool lockDelta = true;
    int lockDeltaMode = 0;

    bool fullGamePrediction = false;
    float acceptablePrediction = 0.45;

    bool speedhackAudio = true;
    bool blockInputs = true;
    bool useRegularBg = false;

    bool automaticVideoName = true;
    bool previewAudio = true;
    std::string videoNameTemplate = "%name%_%rand%";

    bool autoMacroName = true;
    std::string macroNameTemplate = "%name%";

    bool renderLabelsWhileRecording = false;
    std::string lastLoadedPreset = "";
    bool scrollSpeedBugFix = false;

    bool autoclickerEnabled = false;
    int autoclickerFrequency = 1;
    bool autoclickerSwifts = false;
    int autoclickerPlayer = 0;

    bool useAlternateHook = false;

    bool backwardsStepping = false;
    uint32_t stepsToSave = 120;
    bool frameStepperHold = true;
    double frameStepperHoldDelay = 0.35;
    double frameStepperHoldSpeed = 12.0;
    bool showEndMenuButton = true;

    bool noclipTintEnabled = true;
    int noclipPlayer = 0;
    std::array<float, 4> noclipTintColor = {1.0, 0.0, 0.0, 1.0};
    double noclipTintOpacity = 0.35;
    double noclipTintTime = 0.2;

    std::array<float, 4> layoutBgColor = {0.2828, 0.4901, 1.0, 1.0};
    std::array<float, 4> layoutGroundColor = {0.2828, 0.4901, 1.0, 1.0};

    struct TrajectorySettings {
        struct State {
            bool enabled = false;
            std::array<float, 4> colors = {0.0, 1.0, 0.0, 1.0};
        };

        enum Mode {
            Hold = 0x1,
            Swift = 0x2,
            Release = 0x4,

            Left = 0x8,
            Right = 0x10,

            Player1 = 0x20,
            Player2 = 0x40,

            FollowPlayer = 0x80,
            FollowOpposite = 0x100,

            Platformer = 0x200,
        };

        bool enabled = false;
        double width = 0.5;
        double length = 1.0;
        int maxSteps = 0;

        std::unordered_map<int, State> categories = {
            {Mode::Hold, {true}},
            {Mode::Swift, {}},
            {Mode::Release, {true}},

            {Mode::Hold | Mode::Platformer, {}},
            {Mode::Swift | Mode::Platformer, {}},
            {Mode::Release | Mode::Platformer, {}},

            {Mode::Hold | Mode::Left | Mode::Platformer, {}},
            {Mode::Swift | Mode::Left | Mode::Platformer, {}},
            {Mode::Release | Mode::Left | Mode::Platformer, {}},

            {Mode::Hold | Mode::Right | Mode::Platformer, {}},
            {Mode::Swift | Mode::Right | Mode::Platformer, {}},
            {Mode::Release | Mode::Right | Mode::Platformer, {}},

            {Mode::Hold | Mode::FollowPlayer | Mode::Platformer, {true}},
            {Mode::Swift | Mode::FollowPlayer | Mode::Platformer, {}},
            {Mode::Release | Mode::FollowPlayer | Mode::Platformer, {true}},

            {Mode::Hold | Mode::FollowOpposite | Mode::Platformer, {}},
            {Mode::Swift | Mode::FollowOpposite | Mode::Platformer, {}},
            {Mode::Release | Mode::FollowOpposite | Mode::Platformer, {}},
        };
    } trajectory;

    struct HitboxSettings {
        double width = 0.5;
        bool trailEnabled = false;
        bool holdingTrailEnabled = true;
        std::array<float, 4> holdingTrailColor = {0.1f, 1.0f, 0.2f, 1.0f};

        int trailMaxLength = 1000;
        int trailRebuildInterval = 3;

        enum Type {
            Player,
            PlayerRotated,
            PlayerInner,
            PlayerCircle,

            Solid,
            Hazard,
            Passable,
            Interactable,
            InteractableActive
        };

        struct HBState {
            bool enabled = true;
            double fillOpacity = 0.00;
            std::array<float, 4> colors;
        };

        std::unordered_map<int, HBState> categories = {
            {Type::Player, {true, 0.0, {1.0, 0.0, 0.0, 1.0}}},
            {Type::PlayerInner, {true, 0.0, {0.0, 0.0, 1.0, 1.0}}},
            {Type::PlayerRotated, {true, 0.0, {0.5, 0.0, 0.0, 1.0}}},
            {Type::PlayerCircle, {true, 0.0, {1.0, 0.0, 0.0, 1.0}}},

            {Type::Solid, {true, 0.0, {0.0, 0.0, 1.0, 1.0}}},
            {Type::Hazard, {true, 0.0, {1.0, 0.0, 0.0, 1.0}}},
            {Type::Passable, {true, 0.0, {0.0, 1.0, 1.0, 1.0}}},
            {Type::Interactable, {true, 0.0, {1.0, 1.0, 0.0, 1.0}}},
            {Type::InteractableActive, {true, 0.0, {0.2, 1.0, 0.0, 1.0}}},
        };
    } hitboxes;

    static SLSettings* get() {
        static SLSettings instance;
        return &instance;
    }
};

template <>
struct glz::meta<SLSettings> {
    using T = SLSettings;
    static constexpr auto value = object(
        "global_enabled", &T::botEnabled,
        "ui_visible", hide{&T::uiVisible},
        "glass_ui", &T::glassUi,
        "ui_scale", &T::uiScale,
        "ui_opacity", &T::uiOpacity,
        "fit_menu_to_content", &T::fitMenuToContent,
        "animation_speed", &T::animationSpeed,
        "play_animations", &T::playAnimations,
        "pride_mode", &T::rainbowMode,
        "theme", &T::theme,
        "tps", hide{&T::tps},
        "speed", hide{&T::speed},
        "real_time", &T::realTime,
        "max_upr", &T::maxUpr,
        "target_fps", &T::fpsTarget,
        "dynamic_upr", &T::dynamicUpr,
        "trajectory", &T::trajectory,
        "hitboxes", &T::hitboxes,
        "speedhack_audio", &T::speedhackAudio,
        "block_inputs", &T::blockInputs,
        "use_visual_updates", &T::useVisualUpdates,
        "lock_delta", &T::lockDelta,
        "auto_video_name", &T::automaticVideoName,
        "video_name_template", &T::videoNameTemplate,
        "preset", &T::lastLoadedPreset,
        "preview_audio", &T::previewAudio,
        "autoclicker_enabled", hide{&T::autoclickerEnabled},
        "autoclicker_frequency", &T::autoclickerFrequency,
        "autoclicker_swifts", &T::autoclickerSwifts,
        "autoclicker_player", &T::autoclickerPlayer,

        "ssb_fix", &T::scrollSpeedBugFix,

        "backwards_stepping", &T::backwardsStepping,
        "steps_saved", &T::stepsToSave,
        "frame_stepper_hold", &T::frameStepperHold,
        "frame_stepper_hold_delay", &T::frameStepperHoldDelay,
        "frame_stepper_hold_speed", &T::frameStepperHoldSpeed,
        "show_end_menu_button", &T::showEndMenuButton,

        "noclip_tint_enabled", &T::noclipTintEnabled,
        "noclip_player", &T::noclipPlayer,
        "noclip_tint_color", &T::noclipTintColor,
        "noclip_tint_opacity", &T::noclipTintOpacity,
        "noclip_tint_time", &T::noclipTintTime,

        "autosave_at_level_end", &T::autosaveAtLevelEnd,
        "autosave_at_interval", &T::autosaveAtInterval,
        "autosave_interval", &T::autosaveInterval,

        "acceptable_prediction", &T::acceptablePrediction,

        "layout_bg", &T::layoutBgColor,
        "layout_ground", &T::layoutGroundColor,
        "layout_use_bg", &T::useRegularBg,

        "alternate_hook", &T::useAlternateHook,

        "auto_macro_name", &T::autoMacroName,
        "macro_name_template", &T::macroNameTemplate,
        "render_labels_while_recording", &T::renderLabelsWhileRecording
    );
};

$on_mod(DataSaved) {
    std::filesystem::path settingsPath =
        silicate::paths::file("settings.json");
    std::ofstream settingsFd(settingsPath);

    auto settings = SLSettings::get();
    std::string serialized =
        glz::write<glz::opts{.prettify = true}>(*settings).value_or(
            std::string{});
    settingsFd << serialized;

    std::filesystem::path keybindsPath =
        silicate::paths::file("keybinds.json");
    SLBindingManager::get()->writeToFile(keybindsPath);
}
