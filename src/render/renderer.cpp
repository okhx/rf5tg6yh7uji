#include "renderer.hpp"
#include "util/paths.hpp"

#ifdef GEODE_IS_WINDOWS
#include <Zydis/Zydis.h>
#include <immintrin.h>
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/ShaderLayer.hpp>
#include <algorithm>
#include <iostream>
#ifdef GEODE_IS_WINDOWS
#include <safetyhook.hpp>
#endif
#include <string>
#include <thread>

#include "Geode/utils/random.hpp"
#include "Geode/utils/string.hpp"
#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "colorspace/nv12.hpp"
#include "colorspace/rgb0.hpp"
#include "colorspace/rgb24.hpp"
#include "colorspace/yuv420p.hpp"
#include "dsp.hpp"
#include "replay/system.hpp"
#include "ui/manager.hpp"

using namespace geode::prelude;

constexpr int ALIGNMENT = 1;

namespace fs = std::filesystem;

struct RenderOpt {
    std::string m_name;
    std::string m_value;
};

static std::vector<RenderOpt> parseArgs(std::stringstream& in) {
    std::vector<RenderOpt> opts;
    while (!in.eof()) {
        std::string name, value;
        in >> name >> value;

        if (!name.starts_with("-")) {
            return opts;
        }

        name.erase(0, 1);

        if (value.starts_with("\"") && value.ends_with("\"")) {
            value.erase(0, 1);
            value.erase(value.size() - 1, value.size());
        }

        opts.push_back(RenderOpt{
            .m_name = name,
            .m_value = value,
        });
    }

    return opts;
}

void Renderer::saveSettings(fs::path& path) const {
    auto& settings = m_settings;

    (void)glz::write_file_json<glz::opts{.prettify = true}>(
        settings, geode::utils::string::pathToString(path), std::string{});
}

#ifndef GEODE_IS_IOS
enum class GPUVendor {
    NVIDIA,
    AMD,
    INTEL,
    OTHER,
};

static GPUVendor getGPUVendor() {
#ifdef GEODE_IS_WINDOWS
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ,
                      &hKey) != ERROR_SUCCESS) {
        geode::log::info("Detected no GPU - defaulting to libx264 (CPU)");
        return GPUVendor::OTHER;
    }

    char subKeyName[256];
    DWORD idx = 0;

    while (idx < 8192) {
        DWORD size = sizeof(subKeyName);
        LONG result = RegEnumKeyExA(hKey, idx++, subKeyName, &size, nullptr,
                                    nullptr, nullptr, nullptr);

        if (result != ERROR_SUCCESS) {
            break;
        }

        std::string keyName = subKeyName;

        if (keyName.find("VEN_10DE") != std::string::npos) {
            geode::log::info("Detected NVIDIA GPU - defaulting to h264_nvenc");
            RegCloseKey(hKey);
            return GPUVendor::NVIDIA;
        }

        if (keyName.find("VEN_1002") != std::string::npos ||
            keyName.find("VEN_1022") != std::string::npos) {
            geode::log::info("Detected AMD GPU - defaulting to h264_amf");
            RegCloseKey(hKey);
            return GPUVendor::AMD;
        }

        if (keyName.find("VEN_8086") != std::string::npos) {
            geode::log::info("Detected Intel GPU - defaulting to h264_qsv");
            RegCloseKey(hKey);
            return GPUVendor::INTEL;
        }
    }

    geode::log::info("Detected no GPU - defaulting to libx264 (CPU)");
    return GPUVendor::OTHER;
#else
    geode::log::info("Detected non-Windows platform - defaulting to libx264 (CPU)");
    return GPUVendor::OTHER;
#endif
}

static std::string getDefaultCodec() {
    switch (getGPUVendor()) {
        case GPUVendor::NVIDIA:
            return "h264_nvenc";
        case GPUVendor::AMD:
            return "h264_amf";
        case GPUVendor::INTEL:
            return "h264_qsv";
        default:
            return "libx264";
    }
}
#endif

void Renderer::initializeDefaults() {
#ifdef GEODE_IS_IOS
    m_settings.m_fps = 240;
    m_settings.m_codec = "h264";
    m_settings.m_extension = "mp4";
#else
    m_settings.m_codec = getDefaultCodec();
#endif
}

static void silentChangeSize(CCSize size);
static void resizeShaderLayer(CCSize size, CCSize original);

#ifdef GEODE_IS_MOBILE
geode::Result<> Renderer::startMobile() {
    auto* pl = PlayLayer::get();
    if (!pl) return geode::Err("Open a level before starting a render");
    if (pl->m_hasCompletedLevel || pl->m_levelEndAnimationStarted) {
        return geode::Err("Restart the level before starting a render");
    }
#ifndef GEODE_IS_IOS
    if (!Loader::get()->isModLoaded("eclipse.ffmpeg-api")) {
        return geode::Err("Install and enable eclipse.ffmpeg-api to render on mobile");
    }
#endif
    if (m_mobileRecording) return geode::Err("A render is already running");

#ifdef GEODE_IS_IOS
    const auto resolutionIndex = iosRenderResolutionIndex(
        m_settings.m_width, m_settings.m_height);
    const auto& resolution = IOS_RENDER_RESOLUTIONS[resolutionIndex];
    m_settings.m_width = resolution.m_width;
    m_settings.m_height = resolution.m_height;
    m_settings.m_fps = 240;
#else
    m_settings.m_width = std::clamp(m_settings.m_width, 64, 3840);
    m_settings.m_height = std::clamp(m_settings.m_height, 64, 2160);
    m_settings.m_width += m_settings.m_width & 1;
    m_settings.m_height += m_settings.m_height & 1;
    m_settings.m_fps = std::clamp(m_settings.m_fps, 1, 240);
#endif
    m_settings.m_bitrate = std::clamp<uint32_t>(
        m_settings.m_bitrate, 1'000'000, 200'000'000);
    m_collectAudio = m_settings.m_renderAudio;

    int sampleRate = 48000;
    int channels = 2;
    FMODAudioEngine::get()->m_system->getSoftwareFormat(
        &sampleRate, nullptr, &channels);

    std::string name = m_settings.m_outputPath.empty()
        ? "grape-mobile-render" : m_settings.m_outputPath;
    if (m_autoVideoName->inner()) {
        name = pl->m_level->m_levelName.empty()
            ? "grape-mobile-render" : pl->m_level->m_levelName;
    }
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*') c = '_';
    }
    std::string extension = m_settings.m_extension.empty()
        ? "mp4" : m_settings.m_extension;
#ifdef GEODE_IS_IOS
    extension = "mp4";
#endif
    if (extension.front() == '.') extension.erase(extension.begin());
    auto output = silicate::paths::directory("videos") /
                  (name + "." + extension);
    m_mobileOutputPath = output;

#ifdef GEODE_IS_IOS
    m_iosWriter = std::make_unique<IOSVideoWriter>();
    auto result = m_iosWriter->open(
        output, m_settings.m_width, m_settings.m_height,
        m_settings.m_fps, m_settings.m_bitrate, sampleRate, channels,
        m_collectAudio);
    if (result.isErr()) {
        m_iosWriter.reset();
        return result;
    }
#else
    m_mobileRecorder = std::make_unique<MobileFFmpegRecorder>();
    if (!m_mobileRecorder->valid()) {
        m_mobileRecorder.reset();
        return geode::Err("FFmpeg API recorder could not be created");
    }

    ffmpeg::RenderSettings settings;
    settings.m_pixelFormat = ffmpeg::PixelFormat::RGB24;
    settings.m_codec = m_settings.m_codec;
    settings.m_bitrate = m_settings.m_bitrate;
    settings.m_width = static_cast<uint32_t>(m_settings.m_width);
    settings.m_height = static_cast<uint32_t>(m_settings.m_height);
    settings.m_fps = static_cast<uint16_t>(m_settings.m_fps);
    settings.m_outputFile = output;
    settings.m_doVerticalFlip = true;
    auto result = m_mobileRecorder->init(settings);
    if (result.isErr()) {
        m_mobileRecorder.reset();
        return result;
    }
#endif

    std::vector<uint8_t> blank(
        static_cast<size_t>(m_settings.m_width) * m_settings.m_height * 4, 0);
    m_mobileTexture = new CCTexture2D();
    if (!m_mobileTexture->initWithData(
            blank.data(), kCCTexture2DPixelFormat_RGBA8888,
            m_settings.m_width, m_settings.m_height,
            {static_cast<float>(m_settings.m_width),
             static_cast<float>(m_settings.m_height)})) {
        m_mobileTexture->release();
        m_mobileTexture = nullptr;
#ifdef GEODE_IS_IOS
        (void)m_iosWriter->finish();
        m_iosWriter.reset();
#else
        m_mobileRecorder->stop();
        m_mobileRecorder.reset();
#endif
        return geode::Err("Failed to allocate the mobile render texture");
    }

    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glGenFramebuffers(1, &m_mobileFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_mobileFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_mobileTexture->getName(), 0);
    const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        stopMobile();
        return geode::Err("Mobile render framebuffer is incomplete");
    }

#ifndef GEODE_IS_IOS
    m_mobileFrame.resize(
        static_cast<size_t>(m_settings.m_width) * m_settings.m_height * 3);
#else
    m_mobileFrame.clear();
#endif
    m_mobileRGBAFrame.resize(
        static_cast<size_t>(m_settings.m_width) * m_settings.m_height * 4);

    auto* view = CCEGLView::sharedOpenGLView();
    m_mobileOriginalFrameSize = view->getFrameSize();
    const CCSize renderSize{static_cast<float>(m_settings.m_width),
                            static_cast<float>(m_settings.m_height)};
    if (auto* base = GJBaseGameLayer::get();
        base && base->m_shaderLayer && base->m_shaderLayer->m_renderTexture) {
        silentChangeSize(renderSize);
        resizeShaderLayer(renderSize, renderSize);
        silentChangeSize(m_mobileOriginalFrameSize);
        m_mobileShaderResized = true;
    }

#ifdef GEODE_IS_IOS
    if (m_collectAudio) {
        AudioRecorder::get()->init();
        AudioRecorder::get()->attach(m_settings.m_musicVolume,
                                     m_settings.m_sfxVolume);
    }
#endif
    m_mobileNextFrameTime = 0.0;
    m_mobileArmFrame = Bot::get()->updater().getFrame();
    m_mobileStartFrame = 0;
    m_mobileCaptureStarted = false;
    m_mobileSaveError.clear();
    m_endTime = 0.0f;
    m_time = 0.0;
    m_mobileRecording = true;
    m_recording = true;
    geode::log::info("Mobile render started: {}", output);
    return geode::Ok();
}

void Renderer::stopMobile() {
    std::string saveError;
#ifdef GEODE_IS_IOS
    if (AudioRecorder::get()->m_attached) {
        auto& pending = AudioRecorder::get()->m_buffer;
        if (m_iosWriter && !pending.empty()) {
            auto audioResult = m_iosWriter->appendAudio(pending);
            if (audioResult.isErr()) {
                geode::log::error("iOS final audio write failed: {}",
                                  audioResult.unwrapErr());
            }
            if (audioResult.isOk() && audioResult.unwrap()) pending.clear();
        }
        AudioRecorder::get()->detach();
        AudioRecorder::get()->uninit();
    }
    if (m_iosWriter) {
        auto result = m_iosWriter->finish();
        if (result.isErr()) {
            saveError = result.unwrapErr();
            geode::log::error("iOS render finalize failed: {}", saveError);
        } else {
            std::error_code fileError;
            const auto fileSize = std::filesystem::file_size(
                m_mobileOutputPath, fileError);
            geode::log::info(
                "iOS render saved to {} ({} bytes)",
                m_mobileOutputPath,
                fileError ? 0 : fileSize);
        }
        m_iosWriter.reset();
    }
#else
    if (m_mobileRecorder) {
        m_mobileRecorder->stop();
        m_mobileRecorder.reset();
    }
#endif
    if (m_mobileFbo) {
        glDeleteFramebuffers(1, &m_mobileFbo);
        m_mobileFbo = 0;
    }
    if (m_mobileTexture) {
        m_mobileTexture->release();
        m_mobileTexture = nullptr;
    }
    m_mobileFrame.clear();
    m_mobileRGBAFrame.clear();
    auto* base = GJBaseGameLayer::get();
    if (m_mobileShaderResized && base && base->m_shaderLayer &&
        base->m_shaderLayer->m_renderTexture &&
        m_mobileOriginalFrameSize.width > 0.f) {
        silentChangeSize(m_mobileOriginalFrameSize);
        resizeShaderLayer(m_mobileOriginalFrameSize,
                          m_mobileOriginalFrameSize);
        m_mobileShaderResized = false;
    }
    m_mobileCaptureStarted = false;
    m_mobileRecording = false;
    m_recording = false;
    geode::log::info("Mobile render stopped");
    m_mobileSaveError = std::move(saveError);
}

void Renderer::updateMobile(PlayLayer* pl) {
    if (!m_mobileRecording || !pl) return;

    const double tps = std::max(Bot::get()->updater().getTps(), 1.0);
    const uint32_t currentFrame = Bot::get()->updater().getFrame();
    if (!m_mobileCaptureStarted) {
        if (pl->m_isPaused || !pl->m_started) return;

        if (m_mobileArmFrame > 2 && currentFrame >= m_mobileArmFrame) return;

        m_mobileCaptureStarted = true;
        m_mobileStartFrame = currentFrame;
        m_mobileNextFrameTime = 0.0;
#ifdef GEODE_IS_IOS
        if (AudioRecorder::get()->m_attached)
            AudioRecorder::get()->m_buffer.clear();
#endif
        geode::log::info("Mobile renderer capture started at frame {}",
                         currentFrame);
    }

    const double gameTime =
        static_cast<double>(currentFrame - m_mobileStartFrame) / tps;
    if (gameTime + 1e-9 < m_mobileNextFrameTime) return;

    GLint previousFbo = 0;
    GLint viewport[4] = {};
    GLint scissorBox[4] = {};
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);

    auto* director = CCDirector::get();
    auto* view = CCEGLView::sharedOpenGLView();
    const CCSize originalSize = view->getFrameSize();
    const CCSize originalDesign = view->getDesignResolutionSize();
    const auto originalPolicy = view->m_eResolutionPolicy;
    const CCSize renderSize{static_cast<float>(m_settings.m_width),
                            static_cast<float>(m_settings.m_height)};
    view->CCEGLViewProtocol::setFrameSize(renderSize.width, renderSize.height);
    director->updateScreenScale(renderSize);
    view->setDesignResolutionSize(originalDesign.width, originalDesign.height,
                                  kResolutionExactFit);
    director->setViewport();
    director->setProjection(kCCDirectorProjection2D);

    glBindFramebuffer(GL_FRAMEBUFFER, m_mobileFbo);
    glViewport(0, 0, m_settings.m_width, m_settings.m_height);
    glDisable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const bool showCompleteScene =
        m_settings.m_showLevelComplete && pl->m_hasCompletedLevel;
    if (!m_settings.m_renderOnlyLevel || showCompleteScene) {
        auto* scene = CCDirector::get()->m_pRunningScene;
        if (scene) scene->visit();
        else pl->visit();
    } else {
        pl->visit();
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_settings.m_width, m_settings.m_height,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_mobileRGBAFrame.data());

#ifndef GEODE_IS_IOS
    const size_t pixels = static_cast<size_t>(m_settings.m_width) *
                          m_settings.m_height;
    for (size_t i = 0; i < pixels; ++i) {
        m_mobileFrame[i * 3]     = m_mobileRGBAFrame[i * 4];
        m_mobileFrame[i * 3 + 1] = m_mobileRGBAFrame[i * 4 + 1];
        m_mobileFrame[i * 3 + 2] = m_mobileRGBAFrame[i * 4 + 2];
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    view->CCEGLViewProtocol::setFrameSize(originalSize.width,
                                           originalSize.height);
    director->updateScreenScale(originalSize);
    view->setDesignResolutionSize(originalDesign.width, originalDesign.height,
                                  originalPolicy);
    director->setViewport();
    director->setProjection(kCCDirectorProjection2D);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);

#ifdef GEODE_IS_IOS
    if (m_collectAudio && AudioRecorder::get()->m_attached) {
        AudioRecorder::get()->unpause();
        auto& pcm = AudioRecorder::get()->m_buffer;
        if (!pcm.empty()) {
            auto audioResult = m_iosWriter->appendAudio(pcm);
            if (audioResult.isErr()) {
                geode::log::error("Mobile render audio failed: {}",
                                  audioResult.unwrapErr());
                stopMobile();
                return;
            }
            if (audioResult.unwrap()) pcm.clear();
        }
        AudioRecorder::get()->m_time = AudioRecorder::get()->m_fmodTime;
    }
#endif

    int writtenFrames = 0;
    const double frameDuration = 1.0 / m_settings.m_fps;
#ifdef GEODE_IS_IOS
    while (m_mobileNextFrameTime <= gameTime + 1e-9 &&
           writtenFrames < 32) {
        auto result = m_iosWriter->appendRGBA(m_mobileRGBAFrame);
        if (result.isErr()) {
            geode::log::error("Mobile render frame failed: {}",
                              result.unwrapErr());
            stopMobile();
            return;
        }
        ++writtenFrames;
        m_mobileNextFrameTime += frameDuration;
    }
#else
    while (m_mobileNextFrameTime <= gameTime + 1e-9 &&
           writtenFrames < 32) {
        auto result = m_mobileRecorder->writeFrame(m_mobileFrame);
        if (result.isErr()) {
            geode::log::error("Mobile render frame failed: {}",
                              result.unwrapErr());
            stopMobile();
            return;
        }
        ++writtenFrames;
        m_mobileNextFrameTime += frameDuration;
    }
    if (writtenFrames == 32 && m_mobileNextFrameTime <= gameTime)
        m_mobileNextFrameTime = gameTime + frameDuration;
#endif
    m_time = gameTime;

    const bool endMenuShown =
        pl->getChildByID("EndLevelLayer") != nullptr;
    if (pl->m_hasCompletedLevel && endMenuShown) {
        m_endTime += static_cast<float>(writtenFrames * frameDuration);
        if (m_endTime >= m_settings.m_afterEndTime) stopMobile();
    }
}
#endif

void Renderer::loadSettings(fs::path& path) {
    if (!fs::exists(path)) {
        return;
    }

    auto& settings = m_settings;
    auto ec = glz::read_file_json(
        settings, geode::utils::string::pathToString(path), std::string{});
    if (ec) {
        geode::log::error("Failed to read renderer settings: {}",
                          ec.custom_error_message);
        return;
    }

    Bot::get()->ui().m_state.m_bitrate =
        settings.m_bitrate / 1000000.0;
    SLSettings::get()->lastLoadedPreset = path.stem().string();
}

static void silentChangeSize(CCSize size) {
    auto director = CCDirector::sharedDirector();
    auto view = CCEGLView::sharedOpenGLView();
    view->CCEGLViewProtocol::setFrameSize(size.width, size.height);
    director->updateScreenScale(size);
    director->setViewport();
    director->setProjection(kCCDirectorProjection2D);
    glViewport(0, 0, size.width, size.height);
}

static void resizeShaderLayer(CCSize size, CCSize original) {
    ShaderLayer* sh = GJBaseGameLayer::get()->m_shaderLayer;
    sh->m_screenSize = size;
    sh->m_scaleFactor = size.height / CCDirector::get()->getWinSize().height;
    sh->m_aspectRatio = size.width / size.height;

    auto winSize = CCDirector::get()->getWinSize();
    float baseAspectRatio = original.aspect();

    float csf = CCDirector::get()->getContentScaleFactor();
    CCDirector::get()->setContentScaleFactor(1.0);
    sh->m_renderTexture->release();
    sh->m_renderTexture = nullptr;
    sh->m_renderTexture = CCRenderTexture::create(
        size.width / sh->m_aspectRatio * baseAspectRatio, size.height,
        kCCTexture2DPixelFormat_RGBA8888);
    sh->m_renderTexture->retain();
    CCDirector::get()->setContentScaleFactor(csf);
    sh->m_sprite->setTexture(sh->m_renderTexture->getSprite()->getTexture());
    sh->m_textureContentSize = sh->m_sprite->getTexture()->getContentSize();
    geode::log::info("Texture content size: {}", sh->m_textureContentSize);
    sh->m_targetTextureSize = size;
    sh->m_targetTextureSizeExtra = CCSize(0.0f, 0.0f);
    if (baseAspectRatio > sh->m_aspectRatio) {
        float calculatedWidth =
            size.width / sh->m_aspectRatio * baseAspectRatio;
        sh->m_targetTextureSizeExtra =
            cocos2d::CCSize{calculatedWidth - size.width, 0.0};
    } else if (baseAspectRatio < sh->m_aspectRatio) {
        float calculatedHeight =
            size.height / sh->m_aspectRatio * baseAspectRatio;
        sh->m_targetTextureSizeExtra =
            cocos2d::CCSize{0.0, calculatedHeight - size.height};
    }
    geode::log::info("extra={}", sh->m_targetTextureSizeExtra);
    sh->m_sprite->setTextureRect({-1.0f + sh->m_targetTextureSizeExtra.width,
                                  -1.0f + sh->m_targetTextureSizeExtra.height,
                                  size.width + 2.0f, size.height + 2.0f});
    sh->m_state.m_textureScaleX = size.width / winSize.width;
    sh->m_state.m_textureScaleY = size.height / winSize.height;
    geode::log::info("State texture scale: {}", sh->m_scaleFactor);
    geode::log::info("m_state.m_textureScaleX={}", sh->m_state.m_textureScaleX);
    geode::log::info("m_state.m_textureScaleY={}", sh->m_state.m_textureScaleY);
}

geode::Result<> Renderer::start() {
    geode::log::info("Starting renderer");

#ifdef GEODE_IS_MOBILE
    return startMobile();
#endif

    if (!m_ffmpegLoaded || !ff) {
        return geode::Err(
            "Video export needs a mobile encoder backend (FFmpeg/AVFoundation)"
        );
    }

    FMODAudioEngine::get()->m_system->getSoftwareFormat(&m_sampleRate, nullptr,
                                                        &m_channels);
    m_seenFrames = 0;

    std::vector<RenderOpt> args = {};
    {
        std::stringstream s(m_settings.m_renderArgs);
        args = parseArgs(s);
    }

    std::string fileName;
    if (m_autoVideoName->inner()) {
        auto pl = PlayLayer::get();

        std::string formatted(m_videoNameTemplate->inner());

        int randomNumber = geode::utils::random::generate(0, 999'999'999);

        namespace str = geode::utils::string;

        formatted = str::replace(formatted, "%name%", pl->m_level->m_levelName);
        formatted = str::replace(
            formatted, "%id%", std::to_string(pl->m_level->m_levelID.value()));
        formatted =
            str::replace(formatted, "%creator%", pl->m_level->m_creatorName);
        formatted =
            str::replace(formatted, "%rand%", std::to_string(randomNumber));

        fileName = formatted + "." + m_settings.m_extension;
    } else {
        fileName = m_settings.m_outputPath + "." + m_settings.m_extension;
    }

    std::string outPath = geode::utils::string::pathToString(
        silicate::paths::directory("videos") /
        std::filesystem::path(fileName));

    if (m_formatCtx) {
        ff->avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
    }

    {
        ff->avformat_alloc_output_context2(&m_formatCtx, nullptr, nullptr,
                                           outPath.c_str());
    }
    if (!m_formatCtx) {
        return geode::Err("Failed to allocate output context");
    }

    m_videoCodec = ff->avcodec_find_encoder_by_name(m_settings.m_codec.c_str());
    if (!m_videoCodec) {
        return geode::Err("Failed to find codec");
    }

    m_audioCodec =
        ff->avcodec_find_encoder_by_name(m_settings.m_audioCodec.c_str());
    if (!m_audioCodec) {
        return geode::Err("Failed to find audio codec");
    }

    m_videoCodecCtx.reset(ff->avcodec_alloc_context3(m_videoCodec));
    if (!m_videoCodecCtx) {
        return geode::Err("Failed to allocate codec context");
    }

    AVPixelFormat* outPixFmts;
    int outPixFmtCount;

    m_settings.m_pixFmt = AV_PIX_FMT_NONE;
    if (ff->avcodec_get_supported_config(
            m_videoCodecCtx.get(), m_videoCodec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
            (const void**)&outPixFmts, &outPixFmtCount) < 0) {
        return geode::Err("Failed to query codec pix fmt capabilities");
    }

    std::unique_ptr<Colorspace> colorspace;
    for (int i = 0; i < outPixFmtCount; i++) {
        AVPixelFormat fmt = outPixFmts[i];

        switch (fmt) {
            case AV_PIX_FMT_YUV420P: {
                geode::log::info("Using (+) yuv420p");
                m_settings.m_pixFmt = AV_PIX_FMT_YUV420P;
                colorspace = std::make_unique<YUV420PColorspace>();
                break;
            }
            case AV_PIX_FMT_NV12: {
                geode::log::info("Using (+) nv12");
                m_settings.m_pixFmt = AV_PIX_FMT_NV12;
                colorspace = std::make_unique<NV12Colorspace>();
                break;
            }
            case AV_PIX_FMT_RGB0: {
                geode::log::info("Using (+) rgb0");
                m_settings.m_pixFmt = AV_PIX_FMT_RGB0;
                colorspace = std::make_unique<RGB0Colorspace>();
                break;
            }
            case AV_PIX_FMT_RGB24: {
                geode::log::info("Using (+) rgb24");
                m_settings.m_pixFmt = AV_PIX_FMT_RGB24;
                colorspace = std::make_unique<RGB24Colorspace>();
                break;
            }
            default: {
                break;
            }
        }

        if (m_settings.m_pixFmt != AV_PIX_FMT_NONE) {
            break;
        }
    }

    if (m_settings.m_pixFmt == AV_PIX_FMT_NONE) {
        return geode::Err(
            "couldn't find a proper pixel format to use for codec {}",
            m_settings.m_codec);
    }

    m_videoCodecCtx->codec_id = m_videoCodec->id;
    m_videoCodecCtx->pix_fmt = m_settings.m_pixFmt;
    m_videoCodecCtx->width = m_settings.m_width;
    m_videoCodecCtx->height = m_settings.m_height;
    m_videoCodecCtx->bit_rate = m_settings.m_bitrate;
    m_videoCodecCtx->time_base = {1, m_settings.m_fps};
    m_videoCodecCtx->framerate = {m_settings.m_fps, 1};
    m_videoCodecCtx->max_b_frames = 0;

    m_videoCodecCtx->color_primaries = AVCOL_PRI_BT709;
    m_videoCodecCtx->colorspace = AVCOL_SPC_BT709;
    m_videoCodecCtx->color_trc = AVCOL_TRC_BT709;
    m_videoCodecCtx->color_range = AVCOL_RANGE_MPEG;

    if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        m_videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    AVDictionary* video_opts = nullptr;
    if (m_settings.m_codec == "hevc_nvenc") {
        ff->av_dict_set(&video_opts, "b_ref_mode", "0", 0);
    }

    if (m_settings.m_codec == "hevc_qsv" || m_settings.m_codec == "h264_qsv") {
        ff->av_dict_set(&video_opts, "async_depth", "1", 0);
    }

    for (const auto& arg : args) {
        geode::log::info("Applying args {}: {}", arg.m_name, arg.m_value);
        ff->av_dict_set(&video_opts, arg.m_name.c_str(), arg.m_value.c_str(),
                        0);
    }

    geode::log::info("Opening video codec `{}`...", m_settings.m_codec);
    if (ff->avcodec_open2(m_videoCodecCtx.get(), m_videoCodec, &video_opts) <
        0) {
        return geode::Err("Failed to open codec");
    }

    geode::log::info("Allocating audio context...");
    m_audioCodecCtx.reset(ff->avcodec_alloc_context3(m_audioCodec));
    if (!m_audioCodecCtx.get()) {
        return geode::Err("Failed to allocate audio codec context");
    }

    m_audioCodecCtx->codec_tag = 0;
    m_audioCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    m_audioCodecCtx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    m_audioCodecCtx->bit_rate = 320000;
    m_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    m_audioCodecCtx->sample_rate = m_sampleRate;
    m_audioCodecCtx->time_base = {1, m_sampleRate};

    m_audioCodecCtx->flags &= ~AV_CODEC_FLAG_QSCALE;
    if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        m_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    AVDictionary* opts = nullptr;
    ff->av_dict_set(&opts, "compression_level", "0", 0);

    geode::log::info("Opening audio codec `{}`...", m_settings.m_audioCodec);
    if (ff->avcodec_open2(m_audioCodecCtx.get(), m_audioCodec, &opts) < 0) {
        return geode::Err("Failed to open audio codec");
    }

    geode::log::info("Trying to free opts...");
    ff->av_dict_free(&opts);

    geode::log::info("Creating video stream...");
    m_videoStream = ff->avformat_new_stream(m_formatCtx, m_videoCodec);
    if (!m_videoStream) {
        return geode::Err("Failed to create stream");
    }

    if (ff->avcodec_parameters_from_context(m_videoStream->codecpar,
                                            m_videoCodecCtx.get()) < 0) {
        return geode::Err("Failed to copy codec parameters");
    }

    geode::log::info("Creating audio stream...");
    m_audioStream = ff->avformat_new_stream(m_formatCtx, m_audioCodec);
    if (!m_audioStream) {
        return geode::Err("Failed to create audio stream");
    }

    if (ff->avcodec_parameters_from_context(m_audioStream->codecpar,
                                            m_audioCodecCtx.get()) < 0) {
        return geode::Err("Failed to copy audio codec parameters");
    }

    m_audioStream->time_base = m_audioCodecCtx->time_base;

    if (ff->avio_open(&m_formatCtx->pb, outPath.c_str(), AVIO_FLAG_WRITE) < 0) {
        return geode::Err("Failed to open output file");
    }

    if (ff->avformat_write_header(m_formatCtx, nullptr) < 0) {
        return geode::Err("Failed to write header");
    }

    m_frame.reset(ff->av_frame_alloc());
    if (!m_frame) {
        return geode::Err("Failed to allocate frame");
    }

    m_frame->format = m_settings.m_pixFmt;
    m_frame->width = m_settings.m_width;
    m_frame->height = m_settings.m_height;

    m_audioFrame.reset(ff->av_frame_alloc());
    if (!m_audioFrame) {
        return geode::Err("Failed to allocate audio frame");
    }

    m_audioFrame->nb_samples = m_audioCodecCtx->frame_size;
    m_audioFrame->sample_rate = m_audioCodecCtx->sample_rate;
    m_audioFrame->format = m_audioCodecCtx->sample_fmt;
    m_audioFrame->ch_layout = m_audioCodecCtx->ch_layout;
    if (ff->av_frame_get_buffer(m_audioFrame.get(), 0) < 0) {
        return geode::Err("Failed to get audio frame buffer");
    }

    m_pkt.reset(ff->av_packet_alloc());
    if (!m_pkt) {
        return geode::Err("Failed to allocate packet");
    }

    m_audioPkt.reset(ff->av_packet_alloc());
    if (!m_audioPkt) {
        return geode::Err("Failed to allocate packet (audio)");
    }

    m_time = 0.0;
    m_endTime = 0.0;
    m_audioIndex = 0;
    m_updateIndex = 0;
    m_bufferPts = 0;
    m_ptsQueue.clear();

    m_recording = true;
    m_texture.m_width = m_settings.m_width;
    m_texture.m_height = m_settings.m_height;


    m_alignedWidth = (m_settings.m_width + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    m_alignedHeight = (m_settings.m_height + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    geode::log::info("Initializing capture with aligned size: {}x{}",
                     m_alignedWidth, m_alignedHeight);
    m_texture.m_alignedWidth = m_alignedWidth;
    m_texture.m_alignedHeight = m_alignedHeight;

    auto frameSize = CCDirector::get()->getOpenGLView()->getFrameSize();
    silentChangeSize(CCSize(m_alignedWidth, m_alignedHeight));
    resizeShaderLayer(CCSize(m_alignedWidth, m_alignedHeight), frameSize);
    ShaderLayer* sh = GJBaseGameLayer::get()->m_shaderLayer;
    m_texture.m_widthOffset = sh->m_targetTextureSizeExtra.width;
    m_texture.m_heightOffset = sh->m_targetTextureSizeExtra.width;
    silentChangeSize(frameSize);

    m_texture.init(std::move(colorspace));

    m_bufferSize = static_cast<size_t>(m_texture.m_colorspace->getBufferSize());
    geode::log::info("Using buffer size of {}", m_bufferSize);

    m_frameCount = 0;
    m_audioFrameCount = 0;
    m_audioBuffer.clear();

    m_audioStream->time_base = m_audioCodecCtx->time_base;

    AudioRecorder::get()->init();
    AudioRecorder::get()->attach(m_settings.m_musicVolume,
                                 m_settings.m_sfxVolume);

    std::thread(&Renderer::recordLoop, this).detach();

    return geode::Ok();
}

geode::Result<> Renderer::encode(uint8_t* data, size_t size) {
    ff->av_frame_unref(m_frame.get());

    m_frame->format = m_videoCodecCtx->pix_fmt;
    m_frame->width = m_videoCodecCtx->width;
    m_frame->height = m_videoCodecCtx->height;

    int ret = ff->av_frame_get_buffer(m_frame.get(), 1);
    if (ret < 0) {
        return geode::Err("Failed to allocate frame buffer: {}", ret);
    }

    ret = ff->av_frame_make_writable(m_frame.get());
    if (ret < 0) {
        return geode::Err("Failed to make frame writable");
    }

    ff->av_image_fill_linesizes(m_frame->linesize, m_settings.m_pixFmt,
                                m_alignedWidth);

    auto result =
        m_texture.m_colorspace->prepareFrame(m_frame.get(), data, size);
    if (result.isErr()) {
        return result;
    }

    m_frame->pts = m_bufferPts;

    ret = ff->avcodec_send_frame(m_videoCodecCtx.get(), m_frame.get());
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            return geode::Err("Codec requires more input data");
        } else if (ret == AVERROR_EOF) {
            return geode::Err("End of file reached");
        }

        return geode::Err("Failed to send frame: {}", ret);
    }

    return geode::Ok();
}

geode::Result<> Renderer::write() {
    int ret = 0;
    while (ret >= 0) {
        ret = ff->avcodec_receive_packet(m_videoCodecCtx.get(), m_pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            return geode::Err("Failed to receive packet");
        }

        ff->av_packet_rescale_ts(m_pkt.get(), m_videoCodecCtx->time_base,
                                 m_videoStream->time_base);
        m_pkt->stream_index = m_videoStream->index;

        ret = ff->av_interleaved_write_frame(m_formatCtx, m_pkt.get());
        if (ret < 0) {
            return geode::Err("Failed to write frame");
        }

        ff->av_packet_unref(m_pkt.get());
    }


    return geode::Ok();
}

geode::Result<> Renderer::writeAudio(std::vector<float>& data, uint64_t pts) {
    m_audioFrame->pts = pts;
    m_audioFrame->nb_samples = m_audioCodecCtx->frame_size;
    m_audioFrame->ch_layout = m_audioCodecCtx->ch_layout;

    int ret = 0;

    AVChannelLayout inputChLayout;
    ff->av_channel_layout_default(&inputChLayout, m_channels);

    if (!m_swrCtx) {
        ret = ff->swr_alloc_set_opts2(
            &m_swrCtx, &m_audioCodecCtx->ch_layout, m_audioCodecCtx->sample_fmt,
            m_audioCodecCtx->sample_rate, &inputChLayout, AV_SAMPLE_FMT_FLT,
            m_sampleRate, 0, nullptr);
        if (ret < 0) {
            return geode::Err("Failed to set swr options: {}", ret);
        }

        if (!m_swrCtx) {
            return geode::Err("Failed to allocate swr context");
        }

        if (ff->swr_init(m_swrCtx) < 0) {
            return geode::Err("Failed to initialize swr context");
        }
    }

    const uint8_t* inData[1] = {reinterpret_cast<const uint8_t*>(data.data())};
    ff->swr_convert(m_swrCtx, m_audioFrame->data, m_audioCodecCtx->frame_size,
                    inData, m_audioCodecCtx->frame_size);

    ret = ff->avcodec_send_frame(m_audioCodecCtx.get(), m_audioFrame.get());
    if (ret < 0) {
        return geode::Err("Failed to send frame {}", ret);
    }

    while (ret >= 0) {
        ret =
            ff->avcodec_receive_packet(m_audioCodecCtx.get(), m_audioPkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            return geode::Err("Failed to receive packet");
        }

        ff->av_packet_rescale_ts(m_audioPkt.get(), m_audioCodecCtx->time_base,
                                 m_audioStream->time_base);
        m_audioPkt->stream_index = m_audioStream->index;

        ret = ff->av_interleaved_write_frame(m_formatCtx, m_audioPkt.get());
        if (ret < 0) {
            return geode::Err("Failed to write frame");
        }

        ff->av_packet_unref(m_audioPkt.get());
    }

    return geode::Ok();
}

geode::Result<> Renderer::stop() {
    m_recording = false;
    geode::log::info("Detaching audio and stopping all streams...");
    AudioRecorder::get()->detach();
    AudioRecorder::get()->uninit();

    if (m_pkt && m_videoCodecCtx && m_formatCtx && m_videoStream) {
        ff->avcodec_send_frame(m_videoCodecCtx.get(), nullptr);
        while (ff->avcodec_receive_packet(m_videoCodecCtx.get(), m_pkt.get()) ==
               0) {
            ff->av_packet_rescale_ts(m_pkt.get(), m_videoCodecCtx->time_base,
                                     m_videoStream->time_base);
            m_pkt->stream_index = m_videoStream->index;

            int ret = ff->av_interleaved_write_frame(m_formatCtx, m_pkt.get());
            if (ret < 0) {
                return geode::Err("Failed to write frame");
            }

            ff->av_packet_unref(m_pkt.get());
        }
    }

    if (m_audioPkt && m_audioCodecCtx && m_formatCtx && m_audioStream) {
        ff->avcodec_send_frame(m_audioCodecCtx.get(), nullptr);
        while (ff->avcodec_receive_packet(m_audioCodecCtx.get(),
                                          m_audioPkt.get()) == 0) {
            ff->av_packet_rescale_ts(m_audioPkt.get(),
                                     m_audioCodecCtx->time_base,
                                     m_audioStream->time_base);
            m_audioPkt->stream_index = m_audioStream->index;

            int ret =
                ff->av_interleaved_write_frame(m_formatCtx, m_audioPkt.get());
            if (ret < 0) {
                return geode::Err("Failed to write frame");
            }

            ff->av_packet_unref(m_audioPkt.get());
        }
    }

    int ret = ff->av_write_trailer(m_formatCtx);
    if (ret < 0) {
        return geode::Err("Failed to write trailer");
    }

    m_halting = false;
    m_collected = false;

    geode::log::info("Rendering has stopped. Cleaning up");

    if (m_frame) {
        m_frame->data[0] = nullptr;
        m_frame->data[1] = nullptr;
    }

    ff->avio_close(m_formatCtx->pb);
    ff->swr_free(&m_swrCtx);

    ff->avformat_free_context(m_formatCtx);
    m_formatCtx = nullptr;
    m_videoStream = nullptr;
    m_audioStream = nullptr;

    m_texture.destroy();

    return geode::Ok();
}

void Renderer::recordLoop() {
    while (m_recording || m_collected) {
        {
            std::unique_lock<std::mutex> lock(m_recordMutex);
            m_recordCv.wait(lock,
                            [this] { return m_collected || !m_recording; });
        }

        if (m_collected) {
            {
                auto ret = this->encode(m_buffer, m_bufferSize);
                if (ret.isErr()) {
                    geode::log::error("Failed to encode frame: {}",
                                      ret.unwrapErr());
                    break;
                }
            }

            {
                auto ret = this->write();
                if (ret.isErr()) {
                    geode::log::error("Failed to write frame: {}",
                                      ret.unwrapErr());
                    break;
                }
            }

            m_collected = false;
            m_halting = false;
        }
    }

    auto stopResult = this->stop();
    if (stopResult.isErr()) {
        geode::log::error("Failed to stop recording: {}",
                          stopResult.unwrapErr());
    }
}

void Renderer::capture() {
    m_ptsQueue.push_back(m_updateIndex - 1);

    bool delivered = m_texture.capture(&m_buffer);
    if (delivered) {
        m_bufferPts = m_ptsQueue.front();
        m_ptsQueue.pop_front();
        {
            std::lock_guard<std::mutex> lock(m_recordMutex);
            m_collected = true;
        }
        m_recordCv.notify_one();
    } else {
        m_halting = false;
    }
}

void Renderer::update(PlayLayer* pl) {
#ifdef GEODE_IS_MOBILE
    if (m_mobileRecording) {
        updateMobile(pl);
        return;
    }
#endif
    if (!this->isRecording()) return;

    bool started =
        pl->m_started || (m_settings.m_firstAttemptPause && m_seenFrames >= 2);
    m_seenFrames++;
    if (pl->m_isPaused || !started) return;
    if (m_halting || m_collected) return;
    m_halting = true;

    m_time = ++m_updateIndex * this->getDt();

    if (pl->m_hasCompletedLevel &&
        !Bot::get()->replaySystem().getNextQueuedInput().has_value()) {
        if (this->m_endTime >= this->m_settings.m_afterEndTime) {
            this->signalStop();
            return;
        } else {
            this->m_endTime += this->getDt();
        }
    }

    if (m_collectAudio) {
        AudioRecorder::get()->unpause();
    }

    this->capture();
}
