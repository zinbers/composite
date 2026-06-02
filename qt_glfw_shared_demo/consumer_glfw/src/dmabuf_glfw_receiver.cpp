#include "dmabuf_glfw_receiver.h"
#include "dmabuf_channel.h"
#include "dmabuf_defs.h"
#include "egl_dmabuf_import.h"
#include "log.h"

#include <unistd.h>

// ─── Ctor / Dtor ─────────────────────────────────────────────────────────────
DmaBufGlfwReceiver::DmaBufGlfwReceiver()
    : m_channel(new DmaBufChannel())
{
    m_imp[0] = new EglDmaBufImporter();
    m_imp[1] = new EglDmaBufImporter();
}

DmaBufGlfwReceiver::~DmaBufGlfwReceiver()
{
    for (int i = 0; i < 2; ++i) {
        // EGL context may already be gone; best-effort cleanup
        delete m_imp[i];
        m_imp[i] = nullptr;
        if (m_dmabufFds[i] >= 0) {
            close(m_dmabufFds[i]);
            m_dmabufFds[i] = -1;
        }
    }
    delete m_channel;
    m_channel = nullptr;
}

// ─── tryConnect ──────────────────────────────────────────────────────────────
bool DmaBufGlfwReceiver::tryConnect()
{
    if (m_connected) return true;
    if (!m_channel->tryConnect()) return false;
    m_connected = true;
    m_shutdown  = false;
    m_hasFrame  = false;
    return true;
}

// ─── pollFrame ───────────────────────────────────────────────────────────────
bool DmaBufGlfwReceiver::pollFrame(void *eglDpy, int timeoutMs)
{
    if (!m_connected) return false;

    int          rcvFds[DMABUF_SLOT_COUNT] = {-1, -1};
    DmaBufFrameMsg msg{};

    if (!m_channel->recvFrame(msg, rcvFds, DMABUF_SLOT_COUNT, timeoutMs))
        return false;

    // Validate protocol header
    if (msg.magic != DMABUF_MAGIC || msg.version != DMABUF_VERSION) {
        LOG_WARN("DmaBufGlfwReceiver: bad magic/version – ignored");
        for (int i = 0; i < DMABUF_SLOT_COUNT; ++i)
            if (rcvFds[i] >= 0) close(rcvFds[i]);
        return false;
    }

    // Shutdown signal
    if (msg.flags & DMABUF_FLAG_SHUTDOWN) {
        LOG_INFO("DmaBufGlfwReceiver: shutdown received from producer");
        m_shutdown  = true;
        m_connected = false;
        for (int i = 0; i < DMABUF_SLOT_COUNT; ++i)
            if (rcvFds[i] >= 0) close(rcvFds[i]);
        return false;
    }

    // New DMA-BUF fds – import each slot into a GL texture
    if (msg.flags & DMABUF_FLAG_NEW_BUF) {
        // Release any previous fds
        for (int i = 0; i < 2; ++i) {
            if (m_dmabufFds[i] >= 0) { close(m_dmabufFds[i]); m_dmabufFds[i] = -1; }
        }

        bool ok = true;
        for (int i = 0; i < DMABUF_SLOT_COUNT; ++i) {
            if (rcvFds[i] < 0) {
                LOG_ERROR("DmaBufGlfwReceiver: expected fd[%d] but got -1", i);
                ok = false;
                break;
            }
            // Keep a dup so the DMA-BUF stays alive independently of the
            // EGLImage (some drivers may drop the image without the dup).
            m_dmabufFds[i] = dup(rcvFds[i]);

            if (!m_imp[i]->import(eglDpy, rcvFds[i],
                                   static_cast<int>(msg.width),
                                   static_cast<int>(msg.height),
                                   msg.stride, msg.drm_format, msg.modifier)) {
                LOG_ERROR("DmaBufGlfwReceiver: import slot %d failed", i);
                ok = false;
            }
            close(rcvFds[i]); // EGLImage has its own reference now
        }

        if (!ok) return false;

        m_width    = msg.width;
        m_height   = msg.height;
        m_stride   = msg.stride;
        m_fourcc   = msg.drm_format;
        m_modifier = msg.modifier;
    }

    m_activeBuf = msg.buf_index & 1u;
    m_frameId   = msg.frame_id;
    m_hasFrame  = true;
    return true;
}

// ─── textureId ───────────────────────────────────────────────────────────────
unsigned int DmaBufGlfwReceiver::textureId() const
{
    return m_imp[m_activeBuf]->textureId();
}
