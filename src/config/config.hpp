#pragma once
#include "util/storage.hpp"

#include <Geode/Geode.hpp>
#include <fstream>
#include <glaze/glaze.hpp>
#include <string>
#include <unordered_map>

#include "../shared/value/value.hpp"

class GrapeSettings {
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
    std::array<float, 4> grapeAccent = {0.60f, 0.45f, 0.90f, 1.0f};
    std::array<float, 4> skeetAccent = {0.576f, 0.773f, 0.224f, 1.0f};
    std::array<float, 4> skeetGradientLeft = {0.216f, 0.694f, 0.855f, 1.0f};
    std::array<float, 4> skeetGradientMiddle = {0.788f, 0.329f, 0.753f, 1.0f};
    std::array<float, 4> skeetGradientRight = {0.8f, 0.89f, 0.212f, 1.0f};

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
    // Restore Geometry Dash 2.1 physics for levels built before the 2.2
    // changes. Off by default, and every 2.1 branch is gated on it, so with it
    // off the game behaves exactly as it does today.
    bool physics21 = false;

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
    int autoclickerHoldFrames = 1;
    int autoclickerReleaseFrames = 1;
    bool autoclickerSwifts = false;
    bool autoclickerMovingGap = false;
    int autoclickerMovingGapLookahead = 8;
    int autoclickerPlayer = 0;

    // Proportional (triple slider) click timing: one 100% bar split into the
    // relative time spent Holding / Released, plus the share of presses that
    // become swift clicks. Applied over a cycle of autoclickerCycleFrames.
    bool autoclickerProportional = false;
    float autoclickerHoldPct = 50.0f;
    float autoclickerReleasePct = 40.0f;
    float autoclickerSwiftPct = 10.0f;
    int autoclickerCycleFrames = 8;

    bool pathfinderEnabled = false;
    int pathfinderStuckDeaths = 5;

    bool useAlternateHook = false;

    bool backwardsStepping = false;
    uint32_t stepsToSave = 120;
    bool frameStepperHold = true;
    double frameStepperHoldDelay = 0.35;
    double frameStepperHoldSpeed = 12.0;
    double frameStepperArrowOpacity = 0.8;
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
        bool straightEnabled = false;
        std::array<float, 4> straightColor = {0.54f, 0.81f, 0.94f, 1.0f};

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
        bool showOnDeath = false;
        bool trailEnabled = false;
        bool holdingTrailEnabled = true;
        std::array<float, 4> holdingTrailColor = {0.0f, 1.0f, 1.0f, 1.0f};

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

    static GrapeSettings* get() {
        static GrapeSettings instance;
        return &instance;
    }
};

template <>
struct glz::meta<GrapeSettings> {
    using T = GrapeSettings;
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
        "grape_accent", &T::grapeAccent,
        "skeet_accent", &T::skeetAccent,
        "skeet_gradient_left", &T::skeetGradientLeft,
        "skeet_gradient_middle", &T::skeetGradientMiddle,
        "skeet_gradient_right", &T::skeetGradientRight,
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
        "physics_21", &T::physics21,
        "auto_video_name", &T::automaticVideoName,
        "video_name_template", &T::videoNameTemplate,
        "preset", &T::lastLoadedPreset,
        "preview_audio", &T::previewAudio,
        "autoclicker_enabled", hide{&T::autoclickerEnabled},
        "autoclicker_frequency", &T::autoclickerFrequency,
        "autoclicker_hold_frames", &T::autoclickerHoldFrames,
        "autoclicker_release_frames", &T::autoclickerReleaseFrames,
        "autoclicker_swifts", &T::autoclickerSwifts,
        "autoclicker_moving_gap", &T::autoclickerMovingGap,
        "autoclicker_moving_gap_lookahead", &T::autoclickerMovingGapLookahead,
        "autoclicker_player", &T::autoclickerPlayer,
        "autoclicker_proportional", &T::autoclickerProportional,
        "autoclicker_hold_pct", &T::autoclickerHoldPct,
        "autoclicker_release_pct", &T::autoclickerReleasePct,
        "autoclicker_swift_pct", &T::autoclickerSwiftPct,
        "autoclicker_cycle_frames", &T::autoclickerCycleFrames,

        "pathfinder_enabled", hide{&T::pathfinderEnabled},
        "pathfinder_stuck_deaths", &T::pathfinderStuckDeaths,

        "ssb_fix", &T::scrollSpeedBugFix,

        "backwards_stepping", &T::backwardsStepping,
        "steps_saved", &T::stepsToSave,
        "frame_stepper_hold", &T::frameStepperHold,
        "frame_stepper_hold_delay", &T::frameStepperHoldDelay,
        "frame_stepper_hold_speed", &T::frameStepperHoldSpeed,
        "frame_stepper_arrow_opacity", &T::frameStepperArrowOpacity,
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
        grape::paths::file("settings.json");
    std::ofstream settingsFd(settingsPath);

    auto settings = GrapeSettings::get();
    std::string serialized =
        glz::write<glz::opts{.prettify = true}>(*settings).value_or(
            std::string{});
    settingsFd << serialized;

    std::filesystem::path keybindsPath =
        grape::paths::file("keybinds.json");
    BindingManager::get()->writeToFile(keybindsPath);
}
