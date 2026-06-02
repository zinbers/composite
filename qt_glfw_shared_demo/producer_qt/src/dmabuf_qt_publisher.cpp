#include "dmabuf_qt_publisher.h"
#include "dmabuf_channel.h"
#include "dmabuf_defs.h"
#include "egl_dmabuf_export.h"
#include "log.h"

#include <ctime>
#include <unistd.h>

// ─── Ctor / Dtor ─────────────────────────────────────────────────────────────
DmaBufQtPublisher::DmaBufQtPublisher()
    : m_channel(new DmaBufChannel())
{
    m_exp[0] = new EglDmaBufExporter();
    m_exp[1] = new EglDmaBufExporter();
}

DmaBufQtPublisher::~DmaBufQtPublisher()
{
    if (m_ready) shutdown();

    for (int i = 0; i < 2; ++i) {
        if (m_exp[i]) {
            if (m_eglDpy) m_exp[i]->destroy(m_eglDpy);
            delete m_exp[i];
            m_exp[i] = nullptr;
        }
        if (m_dmabufFds[i] >= 0) {
            close(m_dmabufFds[i]);
            m_dmabufFds[i] = -1;
        }
    }
    delete m_channel;
    m_channel = nullptr;
}

// ─── initialize ──────────────────────────────────────────────────────────────
bool DmaBufQtPublisher::initialize(int width, int height,
                                    void *eglDpy, void *eglCtx,
                                    const unsigned int texIds[2])
{
    m_eglDpy = eglDpy;

    for (int i = 0; i < 2; ++i) {
        if (!m_exp[i]->init(eglDpy, eglCtx, texIds[i], width, height)) {
            LOG_ERROR("DmaBufQtPublisher: EglDmaBufExporter[%d] init failed", i);
            return false;
        }
    }

    // Query format from slot 0 (both slots share the same format)
    if (!m_exp[0]->queryFormat(eglDpy, m_fourcc, m_modifier, m_stride)) {
        LOG_ERROR("DmaBufQtPublisher: failed to query DMA-BUF format");
        return false;
    }

    // Pre-export persistent fds (one per slot).  These are kept open so the
    // underlying DMA-BUF objects stay alive across frame boundaries.
    for (int i = 0; i < 2; ++i) {
        m_dmabufFds[i] = m_exp[i]->exportFd(eglDpy);
        if (m_dmabufFds[i] < 0) {
            LOG_ERROR("DmaBufQtPublisher: exportFd[%d] failed", i);
            return false;
        }
    }

    LOG_INFO("DmaBufQtPublisher: initialized  "
             "fmt=0x%08x  mod=0x%016llx  stride=%u",
             m_fourcc,
             static_cast<unsigned long long>(m_modifier),
             m_stride);
    return true;
}

// ─── awaitConsumer ───────────────────────────────────────────────────────────
bool DmaBufQtPublisher::awaitConsumer()
{
    if (!m_channel->listenAndAccept()) return false;
    m_ready      = true;
    m_firstFrame = true;
    return true;
}

// ─── publishFrame ────────────────────────────────────────────────────────────
void DmaBufQtPublisher::publishFrame(int activeSlot, int width, int height)
{
    if (!m_ready) return;

    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);

    DmaBufFrameMsg msg{};
    msg.magic        = DMABUF_MAGIC;
    msg.version      = DMABUF_VERSION;
    msg.width        = static_cast<uint32_t>(width);
    msg.height       = static_cast<uint32_t>(height);
    msg.stride       = m_stride;
    msg.drm_format   = m_fourcc;
    msg.modifier     = m_modifier;
    msg.frame_id     = m_nextFrameId++;
    msg.timestamp_ns = static_cast<uint64_t>(ts.tv_sec)  * 1'000'000'000ULL
                     + static_cast<uint64_t>(ts.tv_nsec);
    msg.flags        = 0;
    msg.buf_index    = static_cast<uint32_t>(activeSlot);

    if (m_firstFrame) {
        // First frame: pass both DMA-BUF fds to the consumer via SCM_RIGHTS
        msg.flags |= DMABUF_FLAG_NEW_BUF;
        m_channel->sendFrame(msg, m_dmabufFds, 2);
        m_firstFrame = false;
        LOG_INFO("DmaBufQtPublisher: sent initial DMA-BUF fds to consumer");
    } else {
        // Subsequent frames: just metadata, no fds (consumer already has them)
        m_channel->sendFrame(msg);
    }
}

// ─── shutdown ────────────────────────────────────────────────────────────────
void DmaBufQtPublisher::shutdown()
{
    if (!m_ready) return;
    m_ready = false;

    DmaBufFrameMsg msg{};
    msg.magic   = DMABUF_MAGIC;
    msg.version = DMABUF_VERSION;
    msg.flags   = DMABUF_FLAG_SHUTDOWN;
    m_channel->sendFrame(msg);
    LOG_INFO("DmaBufQtPublisher: shutdown sent");
}
