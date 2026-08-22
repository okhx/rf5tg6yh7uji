#ifndef RECORDER_DSP_HPP
#define RECORDER_DSP_HPP

#include <Geode/Geode.hpp>
#include <algorithm>
#include <atomic>
#include <vector>

#include "config/config.hpp"

class AudioMonitorRing {
   public:
    void init(size_t capacity) {
        m_data.assign(capacity, 0.0f);
        m_capacity = capacity;
        clear();
    }

    void clear() {
        m_read.store(0, std::memory_order_relaxed);
        m_write.store(0, std::memory_order_relaxed);
    }

    void push(const float* src, size_t n) {
        if (m_capacity == 0) return;
        const size_t w = m_write.load(std::memory_order_relaxed);
        const size_t r = m_read.load(std::memory_order_acquire);
        const size_t space = m_capacity - (w - r);
        if (n > space) n = space;
        for (size_t i = 0; i < n; ++i) {
            m_data[(w + i) % m_capacity] = src[i];
        }
        m_write.store(w + n, std::memory_order_release);
    }

    size_t pop(float* dst, size_t n) {
        if (m_capacity == 0) return 0;
        const size_t r = m_read.load(std::memory_order_relaxed);
        const size_t w = m_write.load(std::memory_order_acquire);
        const size_t take = std::min(n, w - r);
        for (size_t i = 0; i < take; ++i) {
            dst[i] = m_data[(r + i) % m_capacity];
        }
        m_read.store(r + take, std::memory_order_release);
        return take;
    }

   private:
    std::vector<float> m_data;
    size_t m_capacity = 0;
    std::atomic<size_t> m_write{0};
    std::atomic<size_t> m_read{0};
};

class AudioRecorder {
   public:
    static AudioRecorder* get() {
        static AudioRecorder instance;

        return &instance;
    }

    bool init();
    bool refreshFormat();
    bool attach(double musicVolume, double sfxVolume);
    void detach();
    void uninit();

    void appendSamples(const float* data, size_t length);
    void unpause();

    void startMonitor();
    void stopMonitor();

    static FMOD_RESULT F_CALLBACK
    writeCallback(FMOD_DSP_STATE* dspState, float* inBuffer, float* outBuffer,
                  unsigned int length, int inChannels, int* outChannels);

    static FMOD_RESULT F_CALLBACK monitorReadCallback(FMOD_SOUND* sound,
                                                      void* data,
                                                      unsigned int datalen);

    ConfigValuePtr<bool> m_audioPreview = ConfigValue<bool>::create(
        "audio.preview", &GrapeSettings::get()->previewAudio);
    bool m_attached = false;

    FMOD::ChannelGroup* m_master = nullptr;

    std::atomic_bool m_shouldUpdateFmod = false;

    double m_fmodTime = 0.0;
    double m_time = 0.0;
    uint32_t m_index = 0;
    int m_sampleRate = 0;
    int m_channels = 0;
    std::vector<float> m_buffer;

   private:
    FMOD::DSP* m_dsp = nullptr;

    float m_previousMusicVolume = 0.0;
    float m_previousSFXVolume = 0.0;

    FMOD::System* m_monSystem = nullptr;
    FMOD::Sound* m_monSound = nullptr;
    FMOD::Channel* m_monChannel = nullptr;
    AudioMonitorRing m_monRing;
    std::atomic<float> m_monVolume{1.0f};
};

#endif
