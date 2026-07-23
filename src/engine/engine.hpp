#ifndef GRAPE_ENGINE_HPP
#define GRAPE_ENGINE_HPP

#include <array>
#include <memory>

#include "config/config.hpp"
#include "shared/value/value.hpp"


#if defined(GRAPE_DEV_MODE) || defined(SL_DEV_MODE)
#define GRAPE_LOG_DEV(...)               \
    {                                    \
        geode::log::info(__VA_ARGS__);   \
    }
#else
#define GRAPE_LOG_DEV(...) \
    {                       \
    }
#endif

class FrameEngine;
class GameScheduler;
#ifndef GEODE_IS_ANDROID
class UIManager;
#endif
class MacroEngine;
class PracticeFix;
class TrajectoryManager;
class Autoclicker;
class Hitboxes;
class LabelManager;

class GrapeEngine {
   public:
    enum Mode {
        Stopped,
        Recording,
        Playing,
    };

   public:
    static GrapeEngine* get() {
        static GrapeEngine instance;

        return &instance;
    }

    GrapeEngine(GrapeEngine const&) = delete;
    void operator=(GrapeEngine const&) = delete;

    void initialize();
    inline bool initialized() { return m_hasInitialized; };

    bool isEnabled();

    bool isRecording() { return m_mode == Recording; }
    bool isPlaying() { return m_mode == Playing; }
    void setMode(Mode mode) { m_mode = mode; }

    FrameEngine& timeline();
    GameScheduler& clock();
#ifndef GEODE_IS_ANDROID
    UIManager& ui();
#endif
    MacroEngine& macro();
    PracticeFix& practiceFix();
    TrajectoryManager& trajectory();
    Autoclicker& autoclicker();
    Hitboxes& hitboxes();
    LabelManager& labels();

    Mode m_mode = Stopped;
    ConfigValuePtr<bool> m_enabled = ConfigValue<bool>::create(
        "_________________bot.enabled", &GrapeSettings::get()->botEnabled);

   private:
    bool m_hasInitialized = false;

    GrapeEngine();
    ~GrapeEngine();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif
