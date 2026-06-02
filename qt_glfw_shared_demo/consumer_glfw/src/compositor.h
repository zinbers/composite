#pragma once
#include <cstdint>

// Compositor manages all OpenGL state for the GLFW window.
//
// Layout:
//   • Full-screen animated background  (GLFW's own content)
//   • Qt texture overlay in the top-right quadrant
//     – Shows the last received QML frame when connected
//     – Shows a pulsing-red placeholder when disconnected
class Compositor
{
public:
    Compositor();
    ~Compositor();

    // Call once after the GL context is current.
    bool initialize(int windowWidth, int windowHeight);

    // Call every frame (pixel-copy / legacy path).
    //   timeSeconds  – seconds since start (drives animation)
    //   qtConnected  – whether a live producer is attached
    //   qtPixels     – new pixel data to upload (may be nullptr if no new frame)
    //   qtW / qtH    – dimensions of qtPixels
    void renderFrame(float timeSeconds, bool qtConnected,
                     const uint8_t *qtPixels, int qtW, int qtH);

    // Zero-copy DMA-BUF path: the caller provides a GL texture ID that was
    // imported from a DMA-BUF via EglDmaBufImporter.  No pixel upload occurs.
    //   extTexId    – GL texture ID (0 = no texture yet)
    //   hasFrame    – true once at least one frame has been received
    //   flipY       – false for DMA-BUF textures (GPU-native top-origin);
    //                 true when using the pixel-copy path (glReadPixels flips)
    void renderFrameWithExtTex(float timeSeconds, bool qtConnected,
                                unsigned int extTexId, bool hasFrame,
                                bool flipY = false);

    // Call on window resize.
    void resize(int w, int h);

private:
    bool compileShaders();
    void buildQuads();
    void uploadQtTexture(const uint8_t *pixels, int w, int h);

    int m_winW = 0, m_winH = 0;

    unsigned int m_progBg      = 0; // background shader program
    unsigned int m_progOverlay = 0; // Qt overlay shader program

    // Full-screen quad
    unsigned int m_vaoFull = 0, m_vboFull = 0, m_eboFull = 0;
    // Top-right corner quad
    unsigned int m_vaoCorner = 0, m_vboCorner = 0, m_eboCorner = 0;

    // GL texture that holds the most recent Qt frame
    unsigned int m_qtTex  = 0;
    int          m_qtTexW = 0;
    int          m_qtTexH = 0;

    bool m_initialized  = false;
    bool m_hasQtFrame   = false; // true once at least one frame was uploaded
};
