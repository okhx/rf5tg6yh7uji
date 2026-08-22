#include "engine.hpp"
#include "util/storage.hpp"

#include <Geode/Geode.hpp>
#include <filesystem>
#include <algorithm>

#include "assist/autoclicker.hpp"
#include "assist/pathfinder.hpp"
#include "assist/cps.hpp"
#include "assist/hitboxes.hpp"
#include "checkpoint/fix.hpp"
#include "label/label.hpp"
#include "render/renderer.hpp"
#include "replay/macro.hpp"
#include "clock.hpp"
#include "shared/value/value.hpp"
#include "trajectory/trajectory.hpp"
#ifndef GEODE_IS_ANDROID
#include "ui/manager.hpp"
#endif
#include "timeline.hpp"
#ifdef GEODE_IS_MOBILE
#include "ui/touch_overlay.hpp"
#endif

using namespace geode::prelude;

class GrapeEngine::Impl {
    FrameEngine m_timeline;
    GameScheduler m_clock;

#ifndef GEODE_IS_ANDROID
    UIManager m_ui;
#endif

    MacroEngine m_macro;

    PracticeFix m_practiceFix;
    TrajectoryManager m_trajectory;

    Autoclicker m_autoclicker;
    Pathfinder m_pathfinder;
    Hitboxes m_hitboxes;

    LabelManager m_labels;
    CPSCounter m_cps;

    friend class GrapeEngine;
};

#define ENGINE_COMPONENT(ty, name) \
    ty& GrapeEngine::name() { return m_impl->m_##name; }

ENGINE_COMPONENT(GameScheduler, clock)
ENGINE_COMPONENT(FrameEngine, timeline)
#ifndef GEODE_IS_ANDROID
ENGINE_COMPONENT(UIManager, ui)
#endif
ENGINE_COMPONENT(MacroEngine, macro)
ENGINE_COMPONENT(PracticeFix, practiceFix)
ENGINE_COMPONENT(TrajectoryManager, trajectory)
ENGINE_COMPONENT(Autoclicker, autoclicker)
ENGINE_COMPONENT(Pathfinder, pathfinder)
ENGINE_COMPONENT(Hitboxes, hitboxes)
ENGINE_COMPONENT(LabelManager, labels)
ENGINE_COMPONENT(CPSCounter, cps)

GrapeEngine::GrapeEngine() : m_impl(std::make_unique<Impl>()) {}
GrapeEngine::~GrapeEngine() = default;

void GrapeEngine::setMode(Mode mode) {
    const bool modeChanged = m_mode != mode;
    const bool stoppedPlayback = m_mode == Playing && mode != Playing;
    const bool stoppedInputMode = m_mode != Stopped && modeChanged;
    m_mode = mode;
    if (modeChanged) {
        autoclicker().reset();
        pathfinder().reset();
    }
    if (stoppedPlayback) macro().m_forceNextInput = false;
    if (!stoppedInputMode) return;
    if (auto* layer = GJBaseGameLayer::get()) {
        if (layer->m_player1) layer->m_player1->releaseAllButtons();
        if (layer->m_player2) layer->m_player2->releaseAllButtons();
    }
}

void GrapeEngine::loadSettings() {
    const auto settingsPath = grape::paths::file("settings.json");
    auto& settings = *GrapeSettings::get();
    if (auto error = glz::read_file_json(
            settings, settingsPath.string(), std::string{})) {
        geode::log::error("Failed to read settings: {}",
                          glz::format_error(error, std::string{}));
    }

    settings.stepsToSave = std::max<uint32_t>(2, settings.stepsToSave);
    settings.autoclickerFrequency =
        std::max(1, settings.autoclickerFrequency);
    if (settings.autoclickerHoldFrames == 1 &&
        settings.autoclickerReleaseFrames == 1 &&
        settings.autoclickerFrequency > 1) {
        settings.autoclickerHoldFrames = settings.autoclickerFrequency;
        settings.autoclickerReleaseFrames = settings.autoclickerFrequency;
    }
    settings.autoclickerHoldFrames =
        std::max(1, settings.autoclickerHoldFrames);
    settings.autoclickerReleaseFrames =
        std::max(1, settings.autoclickerReleaseFrames);

    auto& clicker = autoclicker();
    clicker.m_holdFrames = settings.autoclickerHoldFrames;
    clicker.m_releaseFrames = settings.autoclickerReleaseFrames;
    clicker.m_performSwifts = settings.autoclickerSwifts;
    clicker.m_movingGap = settings.autoclickerMovingGap;
    clicker.m_movingGapLookahead = std::clamp(
        settings.autoclickerMovingGapLookahead, 1, 30);
    clicker.m_player = static_cast<Autoclicker::PlayerToggle>(
        std::clamp(settings.autoclickerPlayer, 0, 2));

    auto& finder = pathfinder();
    finder.m_proportional = settings.autoclickerProportional;
    finder.m_holdPct = settings.autoclickerHoldPct;
    finder.m_releasePct = settings.autoclickerReleasePct;
    finder.m_swiftPct = settings.autoclickerSwiftPct;
    finder.m_cycleFrames = std::max(2, settings.autoclickerCycleFrames);
    finder.m_stuckDeaths = std::max(1, settings.pathfinderStuckDeaths);

    timeline().m_noclipType = static_cast<FrameEngine::NoclipType>(
        std::clamp(settings.noclipPlayer, 0, 2));
#ifdef GEODE_IS_MOBILE
    settings.useAlternateHook = true;
#endif

    BindingManager::get()->readFromFile(
        grape::paths::file("keybinds.json"));
}

void GrapeEngine::loadPreset() {
    auto* renderer = Renderer::get();
    renderer->initializeDefaults();

    auto& settings = *GrapeSettings::get();
    if (settings.lastLoadedPreset.empty()) return;

    auto presetPath = grape::paths::directory("presets") /
        (settings.lastLoadedPreset + ".json");
    if (!std::filesystem::exists(presetPath)) {
        geode::log::error("Preset {} does not exist",
                          settings.lastLoadedPreset);
        return;
    }

    geode::log::info("Loading preset {}", settings.lastLoadedPreset);
    renderer->loadSettings(presetPath);
#ifndef GEODE_IS_ANDROID
    ui().m_state.m_presetName = settings.lastLoadedPreset;
#endif
}

void GrapeEngine::prepareStorage() {
    const auto& root = grape::paths::dataRoot();
    static constexpr std::array directories = {
        "replays", "videos", "logs", "presets", "scripts",
        "backups", "libraries", "fonts"};

    for (const auto* name : directories) {
        std::error_code error;
        std::filesystem::create_directories(root / name, error);
        if (error) {
            geode::log::error("Could not create {}/{}: {}", root, name,
                              error.message());
        }
    }
}

void GrapeEngine::initialize() {
    prepareStorage();

    geode::Mod* cbf =
        Loader::get()->getInstalledMod("syzzi.click_between_frames");
    if (cbf) {
        cbf->setSettingValue("soft-toggle", true);
        cbf->setSettingValue("physics-bypass", false);
    }

    loadSettings();
    loadPreset();

    m_enabled->handle([&](bool& enabled) {
#ifdef GEODE_IS_MOBILE
        auto& updater = this->timeline();
        auto* playLayer = PlayLayer::get();

        if (!enabled) {
            this->setMode(Stopped);
            if (Renderer::get()->isRecording()) {
                Renderer::get()->signalStop();
            }

            updater.setPaused(false);
            updater.m_stepOnce->inner() = false;
            updater.m_stepBackwards->inner() = false;
            updater.m_tpsOverflow = 0.0;
            updater.m_tps->inner() = 240.0;
            updater.m_tps->notifyChange();
            updater.m_speedhack->inner() = 1.0;
            updater.m_speedhack->notifyChange();

            this->autoclicker().reset();
            this->labels().update(true);
            if (playLayer) {
                if (auto* overlay = TouchOverlay::get()) {
                    overlay->hide();
                }
            }

            if (this->trajectory().exists()) {
                this->trajectory().uninit();
            }
            this->hitboxes().clearTrail();
            this->hitboxes().destroy();
        } else if (playLayer) {
            if (!this->trajectory().exists()) {
                this->trajectory().init(playLayer);
            }
            if (!this->hitboxes().m_initialized) {
                this->hitboxes().init(playLayer);
            }
            this->labels().m_requiresRefresh = true;
        }

        if (enabled) {
            geode::log::info("Successfully enabled Grape.");
        } else {
            geode::log::info("Successfully disabled Grape.");
        }
        return;
#else
        if (PlayLayer::get()) {
            enabled = true;
            return;
        }

        if (!enabled) {
            this->timeline().m_tps->inner() = 240.0;
            this->timeline().m_tps->notifyChange();
        }

        auto patches = Mod::get()->getPatches();
        std::ranges::for_each(patches, [enabled](Patch* p) {
            if (enabled) {
                geode::log::info("Enabling patch at 0x{:x}", p->getAddress());
                (void)p->enable();
            } else {
                geode::log::info("Disabling patch at 0x{:x}", p->getAddress());
                (void)p->disable();
            }
        });

        auto hooks = Mod::get()->getHooks();
        std::ranges::for_each(hooks, [enabled](Hook* h) {
            if (h->getDisplayName() == "cocos2d::CCEGLView::swapBuffers") {
                return;
            }

            if (enabled) {
                (void)h->enable();
            } else {
                (void)h->disable();
            }
        });

        if (enabled) {
            geode::log::info("Successfully enabled Grape.");
        } else {
            geode::log::info("Successfully disabled Grape.");
        }
#endif
    });

    this->macro().m_autosaveInterval->notifyChange();

    m_hasInitialized = true;

    m_enabled->notifyChange();
}

bool GrapeEngine::isEnabled() {
    return m_enabled->inner();
}
