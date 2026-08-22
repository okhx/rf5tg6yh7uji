#include "dsp.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/fmod/fmod.hpp>
#include <cstring>

#include "engine/engine.hpp"
#include "renderer.hpp"

FMOD_RESULT F_CALLBACK AudioRecorder::writeCallback(FMOD_DSP_STATE*,
                                                    float* inBuffer,
                                                    float* outBuffer,
                                                    unsigned int length,
                                                    int inChannels,
                                                    int* outChannels) {
    AudioRecorder* recorder = AudioRecorder::get();

    if (!recorder->m_shouldUpdateFmod) {
        return FMOD_OK;
    }

    const int outputChannels = outChannels ? *outChannels : inChannels;
    GRAPE_LOG_DEV("length: {}, inChannels: {}, outChannels: {}", length,
                  inChannels, outputChannels);

    recorder->appendSamples(inBuffer, length * inChannels);

    if (outBuffer && outputChannels > 0) {
        std::memset(outBuffer, 0,
                    length * outputChannels * sizeof(float));
    }

    if (recorder->m_audioPreview->inner() && recorder->m_monSystem) {
        recorder->m_monRing.push(inBuffer, length * inChannels);
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK AudioRecorder::monitorReadCallback(
    FMOD_SOUND*, void* data, unsigned int datalen) {
    auto* recorder = AudioRecorder::get();
    auto* out = static_cast<float*>(data);
    const unsigned int n = datalen / sizeof(float);
    const size_t got = recorder->m_monRing.pop(out, n);

    const float gain = recorder->m_monVolume.load(std::memory_order_relaxed);
    for (size_t i = 0; i < got; ++i) {
        out[i] *= gain;
    }

    if (got < n) {
        std::memset(out + got, 0, (n - got) * sizeof(float));
    }
    return FMOD_OK;
}

void AudioRecorder::appendSamples(const float* data, size_t length) {
    if (!data || length == 0) return;

    m_buffer.insert(m_buffer.end(), data, data + length);

    for (size_t i = m_buffer.size() - length; i < m_buffer.size(); i++) {
        m_buffer[i] = std::clamp(m_buffer[i], -1.0f, 1.0f);
    }
}

bool AudioRecorder::init() {
    uninit();

    auto* engine = FMODAudioEngine::get();
    auto* system = engine ? engine->m_system : nullptr;
    if (!system ||
        system->getMasterChannelGroup(&m_master) != FMOD_OK || !m_master) {
        geode::log::error("Cannot initialize renderer audio: no FMOD master");
        m_master = nullptr;
        return false;
    }

    FMOD_DSP_DESCRIPTION desc = {};
    std::strncpy(desc.name, "Grape capture", sizeof(desc.name) - 1);
    desc.version = 0x00020000;
    desc.numinputbuffers = 1;
    desc.numoutputbuffers = 1;
    desc.read = AudioRecorder::writeCallback;
    desc.numparameters = 0;

    if (system->createDSP(&desc, &m_dsp) != FMOD_OK || !m_dsp) {
        geode::log::error("Cannot initialize renderer audio DSP");
        m_dsp = nullptr;
        m_master = nullptr;
        return false;
    }
    if (system->setDSPBufferSize(1024, 2) != FMOD_OK) {
        geode::log::warn("Could not set renderer audio DSP buffer size");
    }

    m_time = 0.0;
    m_index = 0;
    m_fmodTime = 0.0;
    m_buffer.clear();
    return true;
}

bool AudioRecorder::refreshFormat() {
    auto* engine = FMODAudioEngine::get();
    auto* system = engine ? engine->m_system : nullptr;
    if (!system) return false;

    int sampleRate = 0;
    int rawSpeakers = 0;
    int channels = 0;
    FMOD_SPEAKERMODE speakerMode = FMOD_SPEAKERMODE_DEFAULT;
    if (system->getSoftwareFormat(&sampleRate, &speakerMode, &rawSpeakers) !=
        FMOD_OK)
        return false;

    if (speakerMode == FMOD_SPEAKERMODE_RAW) {
        channels = rawSpeakers;
    } else if (system->getSpeakerModeChannels(speakerMode, &channels) !=
               FMOD_OK) {
        return false;
    }

    if (sampleRate <= 0 || channels <= 0) return false;
    m_sampleRate = sampleRate;
    m_channels = channels;
    return true;
}

bool AudioRecorder::attach(double musicVolume, double sfxVolume) {
    if (!m_master || !m_dsp) {
        geode::log::error("Cannot attach renderer audio: DSP is unavailable");
        return false;
    }
    if (!refreshFormat()) {
        geode::log::error("Cannot attach renderer audio: invalid FMOD format");
        return false;
    }

    auto* engine = FMODAudioEngine::get();
    if (!engine || !engine->m_system) return false;

    int numDsps = 0;
    if (m_master->getNumDSPs(&numDsps) != FMOD_OK ||
        m_master->addDSP(numDsps, m_dsp) != FMOD_OK) {
        geode::log::error("Cannot attach renderer audio DSP");
        return false;
    }
    m_attached = true;
    m_dsp->setMeteringEnabled(true, false);

    m_previousMusicVolume = engine->getBackgroundMusicVolume();
    m_previousSFXVolume = engine->getEffectsVolume();
    m_master->setPaused(false);
    engine->setEffectsVolume(sfxVolume);
    engine->setBackgroundMusicVolume(musicVolume);

    if (engine->m_system->setOutput(FMOD_OUTPUTTYPE_NOSOUND_NRT) != FMOD_OK) {
        geode::log::error("Cannot attach renderer audio: NRT output failed");
        detach();
        return false;
    }

    if (m_audioPreview->inner()) {
        startMonitor();
        m_monVolume.store(m_previousMusicVolume, std::memory_order_relaxed);
    }

    return true;
}

void AudioRecorder::startMonitor() {
    if (m_monSystem) return;

    if (FMOD::System_Create(&m_monSystem) != FMOD_OK || !m_monSystem) {
        m_monSystem = nullptr;
        geode::log::warn("[monitor] failed to create FMOD system");
        return;
    }

    m_monSystem->setSoftwareFormat(m_sampleRate, FMOD_SPEAKERMODE_DEFAULT, 0);
    if (m_monSystem->init(32, FMOD_INIT_NORMAL, nullptr) != FMOD_OK) {
        m_monSystem->release();
        m_monSystem = nullptr;
        geode::log::warn("[monitor] failed to init FMOD system");
        return;
    }

    m_monRing.init(static_cast<size_t>(m_sampleRate) * m_channels / 2);

    FMOD_CREATESOUNDEXINFO ex = {};
    ex.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
    ex.numchannels = m_channels;
    ex.defaultfrequency = m_sampleRate;
    ex.format = FMOD_SOUND_FORMAT_PCMFLOAT;
    ex.decodebuffersize = 1024;
    ex.length =
        static_cast<unsigned int>(m_sampleRate) * m_channels * sizeof(float);
    ex.pcmreadcallback = &AudioRecorder::monitorReadCallback;

    if (m_monSystem->createSound(
            nullptr, FMOD_OPENUSER | FMOD_CREATESTREAM | FMOD_LOOP_NORMAL, &ex,
            &m_monSound) != FMOD_OK) {
        geode::log::warn("[monitor] failed to create stream");
        stopMonitor();
        return;
    }

    m_monSystem->playSound(m_monSound, nullptr, false, &m_monChannel);
    geode::log::info("[monitor] audio preview running at {}Hz x{}",
                     m_sampleRate, m_channels);
}

void AudioRecorder::stopMonitor() {
    if (m_monChannel) {
        m_monChannel->stop();
        m_monChannel = nullptr;
    }
    if (m_monSound) {
        m_monSound->release();
        m_monSound = nullptr;
    }
    if (m_monSystem) {
        m_monSystem->close();
        m_monSystem->release();
        m_monSystem = nullptr;
    }
    m_monRing.clear();
}

void AudioRecorder::detach() {
    stopMonitor();
    if (!m_attached) return;

    if (m_master && m_dsp) m_master->removeDSP(m_dsp);
    if (m_master) m_master->setPaused(false);

    auto* engine = FMODAudioEngine::get();
    if (engine) {
        engine->setEffectsVolume(m_previousSFXVolume);
        engine->setBackgroundMusicVolume(m_previousMusicVolume);
        if (engine->m_system) {
            engine->m_system->setOutput(FMOD_OUTPUTTYPE_AUTODETECT);
        }
    }

    m_attached = false;
}

void AudioRecorder::uninit() {
    detach();
    if (m_dsp) {
        m_dsp->release();
        m_dsp = nullptr;
    }
    m_master = nullptr;
}

void AudioRecorder::unpause() {
    if (!m_attached) return;

    auto renderer = Renderer::get();
    auto engine = FMODAudioEngine::get();

    m_shouldUpdateFmod = true;
    engine->update(renderer->getDt());
    m_shouldUpdateFmod = false;

    if (m_monSystem) {
        m_monSystem->update();
    }
}
