#pragma once
#include <cstdint>

class DmaBufChannel;
class EglDmaBufExporter;

// DmaBufQtPublisher manages the producer side of the zero-copy DMA-BUF path.
//
// It owns:
//   • Two EglDmaBufExporter objects (one per double-buffer slot) that wrap the
//     GL textures Qt renders into.
//   • A DmaBufChannel (Unix-socket server) for transmitting the DMA-BUF fds
//     and per-frame metadata to the consumer.
//
// Typical lifetime:
//   DmaBufQtPublisher pub;
//   pub.initialize(width, height, eglDisplay, eglContext, texId[0], texId[1]);
//   // After consumer connects:
//   pub.awaitConsumer();
//   // Each frame (after QQuickRenderControl::endFrame + glFinish):
//   pub.publishFrame(activeSlot, width, height);
//
// The publisher never copies pixel data – the consumer imports the same GPU
// memory via the exported DMA-BUF file descriptors.

class DmaBufQtPublisher {
public:
    DmaBufQtPublisher();
    ~DmaBufQtPublisher();

    DmaBufQtPublisher(const DmaBufQtPublisher &) = delete;
    DmaBufQtPublisher &operator=(const DmaBufQtPublisher &) = delete;

    // Wrap the two GL textures in EGLImages and start listening for a consumer.
    // eglDpy / eglCtx are obtained from QNativeInterface::QEGLContext.
    // texIds must point to an array of two valid GL texture objects.
    bool initialize(int width, int height,
                    void *eglDpy, void *eglCtx,
                    const unsigned int texIds[2]);

    // Block until the consumer process connects to the Unix socket.
    // Must be called after initialize().
    bool awaitConsumer();

    // Export the active slot's DMA-BUF fd and send a frame message.
    // Call this after glFinish() so GPU writes are complete.
    // On the very first call the fd array is transmitted to the consumer;
    // subsequent calls send only metadata (the consumer keeps the fds).
    void publishFrame(int activeSlot, int width, int height);

    // Send the shutdown flag and wake the consumer.
    void shutdown();

    bool isReady() const { return m_ready; }

private:
    DmaBufChannel   *m_channel   = nullptr;
    EglDmaBufExporter *m_exp[2]  = {nullptr, nullptr};

    void    *m_eglDpy  = nullptr;
    bool     m_ready   = false;
    bool     m_firstFrame = true;

    uint64_t m_nextFrameId = 1;
    uint32_t m_stride      = 0;
    uint32_t m_fourcc      = 0;
    uint64_t m_modifier    = 0;

    // The DMA-BUF fds we hold open so the consumer can re-import them on
    // reconnect (we close them in the destructor).
    int m_dmabufFds[2] = {-1, -1};
};
