#include "bot.hpp"
#include "util/paths.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <filesystem>
#include <algorithm>

#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "checkpoint/fix.hpp"
#include "label/label.hpp"
#include "render/renderer.hpp"
#include "replay/system.hpp"
#include "scheduler.hpp"
#include "shared/value/value.hpp"
#include "trajectory/trajectory.hpp"
#include "ui/manager.hpp"
#include "updater.hpp"

#ifdef SILICATE_PROTECT
#include "VMProtect/VMProtectSDK.h"
#endif

using namespace geode::prelude;

class Bot::Impl {
    BotUpdater m_updater;
    BotScheduler m_scheduler;

    UIManager m_ui;

    ReplaySystem m_replaySystem;

    PracticeFix m_practiceFix;
    TrajectoryManager m_trajectory;

    Autoclicker m_autoclicker;
    Hitboxes m_hitboxes;

    LabelManager m_labels;

    friend class Bot;
};

#define BOT_GETTER(ty, name) \
    ty& Bot::name() { return m_impl->m_##name; }

BOT_GETTER(BotScheduler, scheduler)
BOT_GETTER(BotUpdater, updater)
BOT_GETTER(UIManager, ui)
BOT_GETTER(ReplaySystem, replaySystem)
BOT_GETTER(PracticeFix, practiceFix)
BOT_GETTER(TrajectoryManager, trajectory)
BOT_GETTER(Autoclicker, autoclicker)
BOT_GETTER(Hitboxes, hitboxes)
BOT_GETTER(LabelManager, labels)

Bot::Bot() : m_impl(std::make_unique<Impl>()) {}
Bot::~Bot() = default;

void Bot::initialize() {
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

    fs::path dir = silicate::paths::dataRoot();

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
        silicate::paths::file("settings.json");
    auto& settings = *SLSettings::get();
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
    this->updater().m_noclipType = static_cast<BotUpdater::NoclipType>(
        std::clamp(settings.noclipPlayer, 0, 2));

#ifdef GEODE_IS_MOBILE
    // The regular queued-input hook depends on desktop-specific timing.
    // Mobile always records through GJBaseGameLayer::handleButton instead.
    settings.useAlternateHook = true;
#endif

    std::filesystem::path keybindsPath =
        silicate::paths::file("keybinds.json");
    SLBindingManager::get()->readFromFile(keybindsPath);

    if (settings.lastLoadedPreset != "") {
        auto presetPath = silicate::paths::directory("presets") /
                          std::string(settings.lastLoadedPreset + ".json");
        if (std::filesystem::exists(presetPath)) {
            geode::log::info("Loading preset {}", settings.lastLoadedPreset);
            Renderer::get()->loadSettings(presetPath);
            Bot::get()->ui().m_state.m_presetName = settings.lastLoadedPreset;
        } else {
            geode::log::error("Preset {} does not exist",
                              settings.lastLoadedPreset);
        }
    } else {
        Renderer::get()->initializeDefaults();
    }

    m_enabled->handle([&](bool& enabled) {
        if (PlayLayer::get()) {
            enabled = true;
            return;
        }

        if (!enabled) {
            this->updater().m_tps->inner() = 240.0;
            this->updater().m_tps->notifyChange();
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
    });

    this->replaySystem().m_autosaveInterval->notifyChange();

    m_hasInitialized = true;

    m_enabled->notifyChange();
}

bool Bot::isEnabled() { return m_enabled->inner(); }
