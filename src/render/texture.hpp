#pragma once

#include <cocos2d.h>

#include <memory>

#include "colorspace/colorspace.hpp"
#include "pass.hpp"

class RenderTexture {
   public:
    static constexpr int RING_SIZE = 3;

    void init(std::unique_ptr<Colorspace> colorspace);
    void destroy();
    bool capture(uint8_t** data);
    void displayPreview();

   public:
    std::unique_ptr<Colorspace> m_colorspace;
    std::vector<RenderPass> m_passes;

    uint32_t m_width, m_height;
    uint32_t m_alignedWidth, m_alignedHeight;
    uint32_t m_widthOffset, m_heightOffset;

    uint32_t m_tex[2];
    uint32_t m_quadVAO, m_quadVBO;

    int m_old_fbo, m_old_rbo;
    uint32_t m_fbo[2];

    uint32_t m_pbo[RING_SIZE];
    bool m_slotMapped[RING_SIZE] = {};
    size_t m_bufferSize = 0;
    int m_writeIndex = 0;     // slot the next readback writes into
    int m_inflightSlot = -1;  // slot awaiting delivery, -1 if none

    uint32_t m_program;
};
