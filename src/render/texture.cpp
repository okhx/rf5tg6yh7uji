#include "texture.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

// #include "colorspace/nv12.hpp"
#include "colorspace/yuv420p.hpp"
// #include "colorspace/rgb0.hpp"
// #include "colorspace/rgb24.hpp"

#ifdef SILICATE_PROTECT
#include "VMProtect/VMProtectSDK.h"
#endif

using namespace cocos2d;

static void silentChangeSize(CCSize size, float /* wOffset */,
                             float /* hOffset */) {
    auto director = CCDirector::sharedDirector();
    auto view = CCEGLView::sharedOpenGLView();
    view->CCEGLViewProtocol::setFrameSize(size.width, size.height);
    director->updateScreenScale(size);
    director->setViewport();
    director->setProjection(kCCDirectorProjection2D);
    glViewport(0, 0, size.width, size.height);
}

void RenderTexture::init(std::unique_ptr<Colorspace> colorspace) {
#ifdef SILICATE_PROTECT
    VMProtectBegin("RenderTexture::init");
#endif

    m_colorspace = std::move(colorspace);
    m_colorspace->m_alignedWidth = m_alignedWidth;
    m_colorspace->m_alignedHeight = m_alignedHeight;

    m_passes = this->m_colorspace->getPasses();

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_old_fbo);

    for (auto& pass : m_passes) {
        pass.initialize();
    }

    m_bufferSize = m_colorspace->getBufferSize();

    glGenBuffers(RING_SIZE, m_pbo);
    for (int i = 0; i < RING_SIZE; ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, m_bufferSize, nullptr,
                     GL_STREAM_READ);
        m_slotMapped[i] = false;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    m_writeIndex = 0;
    m_inflightSlot = -1;

    float vertices[] = {
        // Pos      // UVs
        -1.0f, -1.0f, 0.0f, 0.0f,  // Bottom-left
        1.0f,  -1.0f, 1.0f, 0.0f,  // Bottom-right
        1.0f,  1.0f,  1.0f, 1.0f,  // Top-right
        -1.0f, 1.0f,  0.0f, 1.0f   // Top-left
    };

    glGenVertexArrays(1, &m_quadVAO);
    glBindVertexArray(m_quadVAO);

    glGenBuffers(1, &m_quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_old_fbo);

#ifdef SILICATE_PROTECT
    VMProtectEnd();
#endif
}

bool RenderTexture::capture(uint8_t** data) {
    auto director = cocos2d::CCDirector::sharedDirector();

    CCSize size = director->getOpenGLView()->getFrameSize();
    m_width = size.width;
    m_height = size.height;

    int blend;
    glGetIntegerv(GL_BLEND, &blend);

    const int w = m_writeIndex;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[w]);

    if (m_slotMapped[w]) {
        // dont let the cia take over
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        m_slotMapped[w] = false;
    }

    for (size_t i = 0; i < m_passes.size(); ++i) {
        auto& pass = m_passes[i];
        glBindFramebuffer(GL_FRAMEBUFFER, pass.m_fbo);
        glViewport(0, 0, pass.m_width, pass.m_height);

        if (i == 0) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // actually render the scene
            silentChangeSize(cocos2d::CCSize(pass.m_width, pass.m_height),
                             m_widthOffset, m_heightOffset);

            CCDirector::get()->m_pRunningScene->visit();
        } else {
            glDisable(GL_BLEND);
        }

        if (pass.m_vertexShader && pass.m_fragmentShader) {
            glUseProgram(pass.m_program);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_passes[pass.m_sourceTex].m_tex);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, pass.m_tex);

            glUniform1i(pass.m_uTextureLoc, 1);
            glUniform2f(pass.m_uTexelSizeLoc, 1.0f / pass.m_width,
                        1.0f / pass.m_height);

            glBindVertexArray(m_quadVAO);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glBindVertexArray(0);
        }

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        pass.m_readPixels(0, 0);
    }

    bool delivered = false;
    if (m_inflightSlot >= 0) {
        const int p = m_inflightSlot;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[p]);
        auto* pixelData = static_cast<uint8_t*>(
            glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
        m_slotMapped[p] = (pixelData != nullptr);

        if (pixelData) {
            *data = pixelData;
            delivered = true;
        } else {
            geode::log::error("Failed to map buffer: {}", glGetError());
        }
    }

    m_inflightSlot = w;
    m_writeIndex = (w + 1) % RING_SIZE;

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    silentChangeSize(cocos2d::CCSize(m_width, m_height), 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_old_fbo);

    glUseProgram(0);
    glEnable(GL_BLEND);

    return delivered;
}

void RenderTexture::displayPreview() {
    CCSize size = CCDirector::sharedDirector()->getOpenGLView()->getFrameSize();

    int blend;
    glGetIntegerv(GL_BLEND, &blend);

    glDisable(GL_BLEND);

    glClearColor(0, 0, 0, 1);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_passes[0].m_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_old_fbo);

    glBlitFramebuffer(m_widthOffset, m_heightOffset, m_alignedWidth,
                      m_alignedHeight, 0, 0, size.width, size.height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    if (blend) {
        glEnable(GL_BLEND);
    }
}

void RenderTexture::destroy() {
    for (auto& pass : m_passes) {
        pass.destroy();
    }

    for (int i = 0; i < RING_SIZE; ++i) {
        if (m_slotMapped[i]) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[i]);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            m_slotMapped[i] = false;
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    glDeleteTextures(2, m_tex);
    glDeleteFramebuffers(2, m_fbo);
    glDeleteBuffers(RING_SIZE, m_pbo);
    glDeleteVertexArrays(1, &m_quadVAO);
    glDeleteBuffers(1, &m_quadVBO);
    glDeleteProgram(m_program);

    m_inflightSlot = -1;
    m_writeIndex = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, m_old_fbo);
}
