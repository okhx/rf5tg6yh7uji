#ifndef RENDERER_HPP
#define RENDERER_HPP

#include "../settings/settings.hpp"
#include "../shared/value/value.hpp"
#include "ffmpeg.hpp"
#include "texture.hpp"
#ifdef GEODE_IS_IOS
#include "ios_video_writer.hpp"
#elif defined(GEODE_IS_MOBILE)
#include "mobile_ffmpeg_api.hpp"
#endif

#include <atomic>
#include <array>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <glaze/glaze.hpp>
#include <memory>
#include <mutex>
#include <string>

#include "dsp.hpp"

#ifdef GEODE_IS_IOS
struct IOSRenderResolution {
    const char* m_name;
    int m_width;
    int m_height;
};

inline constexpr std::array<IOSRenderResolution, 4> IOS_RENDER_RESOLUTIONS{{
    {"720p", 1480, 720},
    {"1080p", 2320, 1080},
    {"1440p", 3060, 1440},
    {"2160p", 3840, 1760},
}};

inline size_t iosRenderResolutionIndex(int width, int height) {
    for (size_t index = 0; index < IOS_RENDER_RESOLUTIONS.size(); ++index) {
        const auto& resolution = IOS_RENDER_RESOLUTIONS[index];
        if (width == resolution.m_width && height == resolution.m_height)
            return index;
    }
    return 0;
}
#endif

struct RendererSettings {
#ifdef GEODE_IS_IOS
    int m_width = 1480;
    int m_height = 720;
#else
    int m_width = 3840;
    int m_height = 2160;
#endif
    uint32_t m_bitrate = 68'000'000;
    std::string m_codec = "";
    AVPixelFormat m_pixFmt = AV_PIX_FMT_NV12;

#ifdef GEODE_IS_IOS
    int m_fps = 240;
#else
    int m_fps = 60;
#endif

    float m_afterEndTime = 5.0f;
    bool m_colorFix = true;

    std::string m_outputPath = "output";
    std::string m_extension = "mp4";
    std::string m_audioCodec = "aac";

    std::string m_renderArgs = "";

    double m_musicVolume = 1.0;
    double m_sfxVolume = 1.0;

    bool m_firstAttemptPause = false;
    bool m_renderOnlyLevel = true;
    bool m_showLevelComplete = true;
    bool m_renderAudio = true;
};

template <>
struct glz::meta<RendererSettings> {
    using T = RendererSettings;
    static constexpr auto value = glz::object(
        "width", &T::m_width, "height", &T::m_height, "bitrate", &T::m_bitrate,
        "codec", &T::m_codec, "pix_fmt", &T::m_pixFmt, "fps", &T::m_fps,
        "after_end_time", &T::m_afterEndTime, "color_fix", &T::m_colorFix,
        "extension", &T::m_extension,
        "audio_codec", &T::m_audioCodec,
        "render_args", &T::m_renderArgs, "output_path", hide{&T::m_outputPath},
        "music_volume", &T::m_musicVolume, "sfx_volume", &T::m_sfxVolume,
        "record_paused", &T::m_firstAttemptPause,
        "render_only_level", &T::m_renderOnlyLevel,
        "show_level_complete", &T::m_showLevelComplete,
        "render_audio", &T::m_renderAudio);
};

#define SL_AV_PTR(type) std::unique_ptr<type, std::function<void(type*)>>

#define SL_AV_LEAK(type) [](type*) {}

#define SL_AV_DELETER(type, deleteFunc) \
    [&](type* ptr) {                    \
        if (ptr) {                      \
            deleteFunc(&ptr);           \
        }                               \
    }

#define SL_AV_DELETER2(type, deleteFunc) \
    [&](type* ptr) {                     \
        if (ptr) {                       \
            deleteFunc(ptr);             \
        }                                \
    }

class Renderer {
   public:
    void queueStart() { m_shouldStart = true; }
    void startIfQueued() {
        if (m_shouldStart) {
            m_shouldStart = false;
            auto ret = start();
            if (ret.isErr()) {
                geode::log::error("Failed to start recording: {}",
                                  ret.unwrapErr());
            }
        }
    }
    geode::Result<> start();
    geode::Result<> encode(uint8_t* data, size_t size);
    geode::Result<> write();
    geode::Result<> writeAudio(std::vector<float>& data, uint64_t pts);
    geode::Result<> stop();

    void signalStop() {
#ifdef GEODE_IS_MOBILE
        if (m_mobileRecording) {
            stopMobile();
            return;
        }
#endif
        m_recording = false;
        m_recordCv.notify_one();
    }

    void recordLoop();

    void capture();
    void update(PlayLayer* pl);

    void displayPreview() { m_texture.displayPreview(); }

    RendererSettings m_settings;

    static Renderer* get() {
        static Renderer instance;

        return &instance;
    }

    float getDt() const { return 1. / m_settings.m_fps; }
    bool isRecording() const { return m_recording; }
    float getTime() const { return m_time; }
    inline bool isFFmpegLoaded() const { return m_ffmpegLoaded; }
    const std::string& getMobileSaveError() const {
        return m_mobileSaveError;
    }

    void loadSettings(std::filesystem::path& path);
    void saveSettings(std::filesystem::path& path) const;
    void initializeDefaults();
    void loadFFmpeg() {
        if (ff) {
            free(ff);
        }

        ff = (ff_t*)malloc(sizeof(ff_t));
        if (!ff) {
            return;
        }

        m_ffmpegLoaded = loadFFmpegFunctions(ff);
    }

    bool m_shouldStart = false;
    bool m_collectAudio = true;

#ifdef GEODE_IS_MOBILE
    geode::Result<> startMobile();
    void stopMobile();
    void updateMobile(PlayLayer* pl);
#ifdef GEODE_IS_IOS
    void notifyEndLevelMenuReady();
#endif
#endif

    std::atomic<bool> m_halting = false;
    std::atomic<bool> m_collected = false;

    SLValuePtr<bool> m_autoVideoName = SLValue<bool>::create(
        "renderer.auto_video_name", &SLSettings::get()->automaticVideoName);
    SLValuePtr<std::string> m_videoNameTemplate = SLValue<std::string>::create(
        "renderer.video_name_template", &SLSettings::get()->videoNameTemplate);

    double m_time = 0;
    RenderTexture m_texture;
    ff_t* ff = 0;

   private:

    SL_AV_PTR(AVCodecContext)
    m_videoCodecCtx = {nullptr,
                       SL_AV_DELETER(AVCodecContext, ff->avcodec_free_context)};
    SL_AV_PTR(AVCodecContext)
    m_audioCodecCtx = {nullptr,
                       SL_AV_DELETER(AVCodecContext, ff->avcodec_free_context)};
    AVFormatContext* m_formatCtx = nullptr;

    AVStream* m_videoStream = nullptr;
    AVStream* m_audioStream = nullptr;
    AVBufferRef* m_hwBuffer = nullptr;
    SwrContext* m_swrCtx = nullptr;

    const AVCodec* m_videoCodec = nullptr;
    const AVCodec* m_audioCodec = nullptr;

    SL_AV_PTR(AVFrame)
    m_frame = {nullptr, SL_AV_DELETER(AVFrame, ff->av_frame_free)};
    SL_AV_PTR(AVFrame)
    m_audioFrame = {nullptr, SL_AV_DELETER(AVFrame, ff->av_frame_free)};

    SL_AV_PTR(AVPacket)
    m_pkt = {nullptr, SL_AV_DELETER(AVPacket, ff->av_packet_free)};
    SL_AV_PTR(AVPacket)
    m_audioPkt = {nullptr, SL_AV_DELETER(AVPacket, ff->av_packet_free)};

    float m_visualFps = 60.0f;
    std::atomic<bool> m_recording = false;

    std::mutex m_recordMutex;
    std::condition_variable m_recordCv;

    int m_frameCount = 0;
    int m_audioFrameCount = 0;
    int m_audioIndex = 0;

    int m_updateIndex = 0;
    int m_seenFrames = 0;
    float m_endTime = 0;

    int m_channels;
    int m_sampleRate;

    uint8_t* m_buffer = nullptr;
    size_t m_bufferSize = 0;

    int64_t m_bufferPts = 0;
    std::deque<int64_t> m_ptsQueue;

    int m_alignedWidth = 0;
    int m_alignedHeight = 0;

    bool m_ffmpegLoaded = false;

    std::vector<float> m_audioBuffer;

    std::mutex m_lock;
    std::string m_mobileSaveError;

    friend class AudioRecorder;

#ifdef GEODE_IS_MOBILE
#ifdef GEODE_IS_IOS
    std::unique_ptr<IOSVideoWriter> m_iosWriter;
#else
    std::unique_ptr<MobileFFmpegRecorder> m_mobileRecorder;
#endif
    cocos2d::CCTexture2D* m_mobileTexture = nullptr;
    GLuint m_mobileFbo = 0;
    std::vector<uint8_t> m_mobileFrame;
    std::vector<uint8_t> m_mobileRGBAFrame;
    double m_mobileNextFrameTime = 0.0;
#ifdef GEODE_IS_IOS
    std::chrono::steady_clock::time_point m_mobileCompletionStartedAt{};
    std::chrono::steady_clock::time_point m_mobileEndMenuReadyAt{};
    double m_mobileCompletionTimeline = 0.0;
    bool m_mobileCompletionClockStarted = false;
    bool m_mobileEndMenuReady = false;
    bool m_mobileFinalizing = false;
#endif
    uint32_t m_mobileArmFrame = 0;
    uint32_t m_mobileStartFrame = 0;
    cocos2d::CCSize m_mobileOriginalFrameSize{};
    std::filesystem::path m_mobileOutputPath;
    bool m_mobileCaptureStarted = false;
    bool m_mobileShaderResized = false;
    bool m_mobileRecording = false;
#endif
};

#endif
