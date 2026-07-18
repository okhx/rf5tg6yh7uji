#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/Event.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ffmpeg {
inline namespace v2 {
enum class HardwareAccelerationType : int { NONE = 0 };
enum class PixelFormat : int { NONE = -1, YUV420P = 0, YUYV422 = 1, RGB24 = 2 };

struct RenderSettings {
    HardwareAccelerationType m_hardwareAccelerationType =
        HardwareAccelerationType::NONE;
    PixelFormat m_pixelFormat = PixelFormat::RGB24;
    std::string m_codec;
    std::string m_colorspaceFilters;
    bool m_doVerticalFlip = true;
    int64_t m_bitrate = 30'000'000;
    uint32_t m_width = 1920;
    uint32_t m_height = 1080;
    uint16_t m_fps = 60;
    std::filesystem::path m_outputFile;
};
}
}

namespace ffmpeg::events::impl {
class Dummy {};

class CreateRecorderEvent
    : public geode::Event<CreateRecorderEvent, bool(CreateRecorderEvent*)> {
    void* m_ptr = nullptr;
   public:
    void setPtr(void* ptr) { m_ptr = ptr; }
    void* getPtr() const { return m_ptr; }
};

class DeleteRecorderEvent
    : public geode::Event<DeleteRecorderEvent, bool(DeleteRecorderEvent*)> {
    void* m_ptr;
   public:
    explicit DeleteRecorderEvent(void* ptr) : m_ptr(ptr) {}
    void* getPtr() const { return m_ptr; }
};

class InitRecorderEvent
    : public geode::Event<InitRecorderEvent, bool(InitRecorderEvent*)> {
    ffmpeg::RenderSettings const* m_settings;
    void* m_ptr;
    geode::Result<> m_result = geode::Err("FFmpeg API event was not handled");
   public:
    InitRecorderEvent(void* ptr, ffmpeg::RenderSettings const* settings)
      : m_settings(settings), m_ptr(ptr) {}
    void setResult(geode::Result<>&& result) { m_result = std::move(result); }
    geode::Result<> getResult() { return m_result; }
    void* getPtr() const { return m_ptr; }
    ffmpeg::RenderSettings const& getRenderSettings() const {
        return *m_settings;
    }
};

class StopRecorderEvent
    : public geode::Event<StopRecorderEvent, bool(StopRecorderEvent*)> {
    void* m_ptr;
   public:
    explicit StopRecorderEvent(void* ptr) : m_ptr(ptr) {}
    void* getPtr() const { return m_ptr; }
};

class GetWriteFrameFunctionEvent
    : public geode::Event<GetWriteFrameFunctionEvent,
                          bool(GetWriteFrameFunctionEvent*)> {
   public:
    using WriteFrame = geode::Result<>(Dummy::*)(std::vector<uint8_t> const&);
   private:
    WriteFrame m_function = nullptr;
   public:
    void setFunction(WriteFrame function) { m_function = function; }
    WriteFrame getFunction() const { return m_function; }
};
}

class MobileFFmpegRecorder {
    ffmpeg::events::impl::Dummy* m_ptr = nullptr;
   public:
    MobileFFmpegRecorder() {
        ffmpeg::events::impl::CreateRecorderEvent event;
        event.send(&event);
        m_ptr = static_cast<ffmpeg::events::impl::Dummy*>(event.getPtr());
    }
    ~MobileFFmpegRecorder() {
        if (m_ptr) {
            ffmpeg::events::impl::DeleteRecorderEvent event(m_ptr);
            event.send(&event);
        }
    }
    bool valid() const { return m_ptr != nullptr; }
    geode::Result<> init(ffmpeg::RenderSettings const& settings) {
        ffmpeg::events::impl::InitRecorderEvent event(m_ptr, &settings);
        event.send(&event);
        return event.getResult();
    }
    geode::Result<> writeFrame(std::vector<uint8_t> const& frame) {
        static auto function = [] {
            ffmpeg::events::impl::GetWriteFrameFunctionEvent event;
            event.send(&event);
            return event.getFunction();
        }();
        if (!function || !m_ptr) return geode::Err("FFmpeg API writer unavailable");
        return std::invoke(function, m_ptr, frame);
    }
    void stop() {
        if (m_ptr) {
            ffmpeg::events::impl::StopRecorderEvent event(m_ptr);
            event.send(&event);
        }
    }
};
