#include "engine.hpp"
#include "util/storage.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <filesystem>
#include <algorithm>

#include "assist/autoclicker.hpp"
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

#ifdef SILICATE_PROTECT
#include "VMProtect/VMProtectSDK.h"
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
    Hitboxes m_hitboxes;

    LabelManager m_labels;

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
ENGINE_COMPONENT(Hitboxes, hitboxes)
ENGINE_COMPONENT(LabelManager, labels)

GrapeEngine::GrapeEngine() : m_impl(std::make_unique<Impl>()) {}
GrapeEngine::~GrapeEngine() = default;

void GrapeEngine::initialize() {
#ifdef SILICATE_PROTECT
    VMProtectBegin("BotInitialize");

    if (!VMProtectIsProtected()) {
        MessageBoxA(NULL, "IMPORTANT NOTICE",
                    "You're running an unprotected version of Grape. Be "
                    "careful about sharing this file with other users.",
                    1);
    }

    VMProtectEnd();
#endif
    namespace fs = std::filesystem;

    fs::path dir = grape::paths::dataRoot();

    fs::create_directories(dir / "replays");
    fs::create_directories(dir / "videos");
    fs::create_directories(dir / "logs");
    fs::create_directories(dir / "presets");
    fs::create_directories(dir / "scripts");
    fs::create_directories(dir / "backups");
    fs::create_directories(dir / "libraries");
    fs::create_directories(dir / "fonts");

    geode::Mod* cbf =
        Loader::get()->getInstalledMod("syzzi.click_between_frames");
    if (cbf) {
        cbf->setSettingValue("soft-toggle", true);
        cbf->setSettingValue("physics-bypass", false);
    }

    std::filesystem::path settingsPath =
        grape::paths::file("settings.json");
    auto& settings = *GrapeSettings::get();
    auto ec =
        glz::read_file_json(settings, settingsPath.string(), std::string{});
    if (ec) {
        geode::log::error("Failed to read settings");
    }

    settings.stepsToSave = std::max<uint32_t>(2, settings.stepsToSave);
    settings.autoclickerFrequency =
        std::max(1, settings.autoclickerFrequency);
    this->autoclicker().m_frequency = settings.autoclickerFrequency;
    this->autoclicker().m_performSwifts = settings.autoclickerSwifts;
    this->autoclicker().m_player = static_cast<Autoclicker::PlayerToggle>(
        std::clamp(settings.autoclickerPlayer, 0, 2));
    this->timeline().m_noclipType = static_cast<FrameEngine::NoclipType>(
        std::clamp(settings.noclipPlayer, 0, 2));

#ifdef GEODE_IS_MOBILE
    settings.useAlternateHook = true;
#endif

    std::filesystem::path keybindsPath =
        grape::paths::file("keybinds.json");
    BindingManager::get()->readFromFile(keybindsPath);

    if (settings.lastLoadedPreset != "") {
        auto presetPath = grape::paths::directory("presets") /
                          std::string(settings.lastLoadedPreset + ".json");
        if (std::filesystem::exists(presetPath)) {
            geode::log::info("Loading preset {}", settings.lastLoadedPreset);
            Renderer::get()->loadSettings(presetPath);
#ifndef GEODE_IS_ANDROID
            GrapeEngine::get()->ui().m_state.m_presetName = settings.lastLoadedPreset;
#endif
        } else {
            geode::log::error("Preset {} does not exist",
                              settings.lastLoadedPreset);
        }
    } else {
        Renderer::get()->initializeDefaults();
    }

    m_enabled->handle([&](bool& enabled) {
#ifdef GEODE_IS_MOBILE
        auto& updater = this->timeline();
        auto* playLayer = PlayLayer::get();

        if (!enabled) {
            this->m_mode = Stopped;
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

bool GrapeEngine::isEnabled() { return m_enabled->inner(); }
