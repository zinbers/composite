#pragma once
#include <cstdint>

class DmaBufChannel;
class EglDmaBufImporter;

// DmaBufGlfwReceiver manages the consumer side of the zero-copy DMA-BUF path.
//
// It owns:
//   • A DmaBufChannel (Unix-socket client) for receiving DMA-BUF fds and
//     per-frame metadata from the producer.
//   • Two EglDmaBufImporter objects (one per double-buffer slot).
//
// The importers create EGLImageKHR objects from the received DMA-BUF fds and
// expose them as plain OpenGL texture IDs – no CPU copy ever occurs.
//
// Typical usage:
//   DmaBufGlfwReceiver rx;
//   // In the render loop:
//   if (!rx.isConnected()) { rx.tryConnect(); continue; }
//   if (rx.pollFrame(1)) {
//       GLuint tex = rx.textureId();  // use in Compositor
//   }

class DmaBufGlfwReceiver {
public:
    DmaBufGlfwReceiver();
    ~DmaBufGlfwReceiver();

    DmaBufGlfwReceiver(const DmaBufGlfwReceiver &) = delete;
    DmaBufGlfwReceiver &operator=(const DmaBufGlfwReceiver &) = delete;

    // Single non-blocking connection attempt to the producer's Unix socket.
    bool tryConnect();

    // Block-wait for the next frame message, up to timeoutMs milliseconds.
    // Returns true when a new frame arrived.
    // eglDpy is needed for EGLImage creation on the first frame (and on any
    // reconnect where DMABUF_FLAG_NEW_BUF is set).
    bool pollFrame(void *eglDpy, int timeoutMs = 16);

    // GL texture ID of the most recently active double-buffer slot.
    // Valid only after at least one successful pollFrame() call.
    unsigned int textureId() const;

    uint64_t frameId()  const { return m_frameId;     }
    bool isConnected()  const { return m_connected;   }
    bool isShutdown()   const { return m_shutdown;    }
    bool hasFrame()     const { return m_hasFrame;    }

private:
    DmaBufChannel   *m_channel      = nullptr;
    EglDmaBufImporter *m_imp[2]     = {nullptr, nullptr};

    bool     m_connected = false;
    bool     m_shutdown  = false;
    bool     m_hasFrame  = false;
    uint64_t m_frameId   = 0;
    uint32_t m_activeBuf = 0;

    // DMA-BUF fds received from the producer (kept open for lifetime of import)
    int m_dmabufFds[2] = {-1, -1};

    // Last known frame metadata (needed to re-import after a format change)
    uint32_t m_width   = 0;
    uint32_t m_height  = 0;
    uint32_t m_stride  = 0;
    uint32_t m_fourcc  = 0;
    uint64_t m_modifier = 0;
};
