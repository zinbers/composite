#include "ipc_bridge.h"
#include "ipc_protocol.h"
#include "log.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>

// ─── Internal state ──────────────────────────────────────────────────────────
struct IpcBridge::Impl {
    int   shmFd  = -1;
    void *shmPtr = MAP_FAILED;

    sem_t *semNewFrame      = SEM_FAILED;
    sem_t *semConsumerReady = SEM_FAILED;
    sem_t *semShutdown      = SEM_FAILED;

    bool     isProducer  = false;
    uint64_t nextFrameId = 1;

    SharedHeader *header() const
    {
        return reinterpret_cast<SharedHeader *>(shmPtr);
    }

    uint8_t *pixelBuffer(int idx) const
    {
        return reinterpret_cast<uint8_t *>(shmPtr)
               + sizeof(SharedHeader)
               + static_cast<size_t>(idx) * PIXEL_BUF_SIZE;
    }
};

// ─── Ctor / Dtor ─────────────────────────────────────────────────────────────
IpcBridge::IpcBridge() : m_impl(new Impl()) {}

IpcBridge::~IpcBridge()
{
    if (!m_impl) return;

    if (m_impl->shmPtr != MAP_FAILED)
        munmap(m_impl->shmPtr, SHM_TOTAL_SIZE);

    if (m_impl->shmFd >= 0) {
        close(m_impl->shmFd);
        if (m_impl->isProducer)
            shm_unlink(SHM_NAME);
    }

    auto closeSem = [&](sem_t *&s, const char *name) {
        if (s != SEM_FAILED) {
            sem_close(s);
            if (m_impl->isProducer)
                sem_unlink(name);
            s = SEM_FAILED;
        }
    };
    closeSem(m_impl->semNewFrame,      SEM_NEW_FRAME);
    closeSem(m_impl->semConsumerReady, SEM_CONSUMER_READY);
    closeSem(m_impl->semShutdown,      SEM_SHUTDOWN);

    delete m_impl;
    m_impl = nullptr;
}

// ─── Producer init ───────────────────────────────────────────────────────────
bool IpcBridge::initProducer()
{
    m_impl->isProducer = true;

    // Remove stale objects from a previous crashed run
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NEW_FRAME);
    sem_unlink(SEM_CONSUMER_READY);
    sem_unlink(SEM_SHUTDOWN);

    // Create shared memory
    m_impl->shmFd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (m_impl->shmFd < 0) {
        LOG_ERROR("shm_open create failed: %s", strerror(errno));
        return false;
    }
    if (ftruncate(m_impl->shmFd, static_cast<off_t>(SHM_TOTAL_SIZE)) < 0) {
        LOG_ERROR("ftruncate failed: %s", strerror(errno));
        return false;
    }
    m_impl->shmPtr = mmap(nullptr, SHM_TOTAL_SIZE,
                          PROT_READ | PROT_WRITE, MAP_SHARED, m_impl->shmFd, 0);
    if (m_impl->shmPtr == MAP_FAILED) {
        LOG_ERROR("mmap failed: %s", strerror(errno));
        return false;
    }

    // Initialise header
    auto *hdr = m_impl->header();
    std::memset(hdr, 0, sizeof(SharedHeader));
    hdr->magic   = PROTO_MAGIC;
    hdr->version = PROTO_VERSION;

    // Create semaphores (initial value = 0)
    m_impl->semNewFrame = sem_open(SEM_NEW_FRAME, O_CREAT | O_EXCL, 0600, 0);
    if (m_impl->semNewFrame == SEM_FAILED) {
        LOG_ERROR("sem_open(%s) failed: %s", SEM_NEW_FRAME, strerror(errno));
        return false;
    }
    m_impl->semConsumerReady = sem_open(SEM_CONSUMER_READY, O_CREAT | O_EXCL, 0600, 0);
    if (m_impl->semConsumerReady == SEM_FAILED) {
        LOG_ERROR("sem_open(%s) failed: %s", SEM_CONSUMER_READY, strerror(errno));
        return false;
    }
    m_impl->semShutdown = sem_open(SEM_SHUTDOWN, O_CREAT | O_EXCL, 0600, 0);
    if (m_impl->semShutdown == SEM_FAILED) {
        LOG_ERROR("sem_open(%s) failed: %s", SEM_SHUTDOWN, strerror(errno));
        return false;
    }

    m_valid = true;
    LOG_INFO("IpcBridge producer ready  (shm ~%zu MB)", SHM_TOTAL_SIZE >> 20);
    return true;
}

// ─── Consumer init (single attempt) ─────────────────────────────────────────
bool IpcBridge::initConsumerOnce()
{
    m_impl->isProducer = false;

    m_impl->shmFd = shm_open(SHM_NAME, O_RDWR, 0);
    if (m_impl->shmFd < 0) return false; // producer not up yet

    m_impl->shmPtr = mmap(nullptr, SHM_TOTAL_SIZE,
                          PROT_READ | PROT_WRITE, MAP_SHARED, m_impl->shmFd, 0);
    if (m_impl->shmPtr == MAP_FAILED) {
        close(m_impl->shmFd);
        m_impl->shmFd = -1;
        return false;
    }

    auto *hdr = m_impl->header();
    if (hdr->magic != PROTO_MAGIC || hdr->version != PROTO_VERSION) {
        LOG_WARN("IpcBridge: shared memory magic/version mismatch – ignoring");
        munmap(m_impl->shmPtr, SHM_TOTAL_SIZE);
        m_impl->shmPtr = MAP_FAILED;
        close(m_impl->shmFd);
        m_impl->shmFd = -1;
        return false;
    }

    m_impl->semNewFrame      = sem_open(SEM_NEW_FRAME,      0);
    m_impl->semConsumerReady = sem_open(SEM_CONSUMER_READY, 0);
    m_impl->semShutdown      = sem_open(SEM_SHUTDOWN,       0);

    if (m_impl->semNewFrame      == SEM_FAILED ||
        m_impl->semConsumerReady == SEM_FAILED ||
        m_impl->semShutdown      == SEM_FAILED)
    {
        if (m_impl->semNewFrame      != SEM_FAILED) { sem_close(m_impl->semNewFrame);      m_impl->semNewFrame      = SEM_FAILED; }
        if (m_impl->semConsumerReady != SEM_FAILED) { sem_close(m_impl->semConsumerReady); m_impl->semConsumerReady = SEM_FAILED; }
        if (m_impl->semShutdown      != SEM_FAILED) { sem_close(m_impl->semShutdown);      m_impl->semShutdown      = SEM_FAILED; }
        munmap(m_impl->shmPtr, SHM_TOTAL_SIZE);
        m_impl->shmPtr = MAP_FAILED;
        close(m_impl->shmFd);
        m_impl->shmFd = -1;
        return false;
    }

    m_valid = true;
    LOG_INFO("IpcBridge consumer connected");
    return true;
}

// ─── Consumer init (blocking with retry) ────────────────────────────────────
bool IpcBridge::initConsumer(int timeoutSeconds)
{
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (true) {
        if (initConsumerOnce()) return true;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - start.tv_sec >= timeoutSeconds) {
            LOG_ERROR("IpcBridge: timeout waiting for producer (%ds)", timeoutSeconds);
            return false;
        }
        usleep(100000); // 100 ms
    }
}

// ─── Signalling ──────────────────────────────────────────────────────────────
void IpcBridge::signalConsumerReady()
{
    if (m_valid) sem_post(m_impl->semConsumerReady);
}

void IpcBridge::signalShutdown()
{
    if (!m_valid) return;
    m_impl->header()->flags |= FLAG_SHUTDOWN;
    sem_post(m_impl->semShutdown);
    sem_post(m_impl->semNewFrame); // wake a blocked consumer
}

// ─── publishFrame ────────────────────────────────────────────────────────────
void IpcBridge::publishFrame(const uint8_t *pixels, uint32_t width, uint32_t height)
{
    if (!m_valid) return;

    auto  *hdr     = m_impl->header();
    // Write into the buffer that the consumer is NOT currently reading
    uint32_t writeBuf = 1u - (hdr->writeBuffer & 1u);

    const size_t sz = static_cast<size_t>(width) * height * CHANNELS;
    if (sz > PIXEL_BUF_SIZE) {
        LOG_WARN("publishFrame: frame (%ux%u) exceeds buffer – truncating",
                 width, height);
    }
    std::memcpy(m_impl->pixelBuffer(static_cast<int>(writeBuf)), pixels,
                sz < PIXEL_BUF_SIZE ? sz : PIXEL_BUF_SIZE);

    hdr->width       = width;
    hdr->height      = height;
    hdr->pixelFormat = 0x8058u; // GL_RGBA8
    hdr->producerPid = static_cast<uint32_t>(getpid());
    hdr->frameId     = m_impl->nextFrameId++;
    hdr->flags       = FLAG_DATA_VALID;
    // Publish the buffer index last (acts as a store-release)
    hdr->writeBuffer = writeBuf;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    hdr->timestampNs = static_cast<uint64_t>(ts.tv_sec)  * 1'000'000'000ULL
                     + static_cast<uint64_t>(ts.tv_nsec);

    sem_post(m_impl->semNewFrame);
}

// ─── waitForFrame ────────────────────────────────────────────────────────────
bool IpcBridge::waitForFrame(int timeoutMs)
{
    if (!m_valid) return false;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  +=  timeoutMs / 1000;
    ts.tv_nsec += (timeoutMs % 1000) * 1'000'000L;
    if (ts.tv_nsec >= 1'000'000'000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1'000'000'000L;
    }

    int rc = sem_timedwait(m_impl->semNewFrame, &ts);
    if (rc != 0 && errno != ETIMEDOUT)
        LOG_WARN("sem_timedwait: %s", strerror(errno));
    return rc == 0;
}

// ─── Accessors ───────────────────────────────────────────────────────────────
const uint8_t *IpcBridge::framePixels() const
{
    return m_impl->pixelBuffer(static_cast<int>(m_impl->header()->writeBuffer & 1u));
}
uint32_t IpcBridge::frameWidth()  const { return m_impl->header()->width;   }
uint32_t IpcBridge::frameHeight() const { return m_impl->header()->height;  }
uint64_t IpcBridge::frameId()     const { return m_impl->header()->frameId; }
bool     IpcBridge::isShutdown()  const
{
    return (m_impl->header()->flags & FLAG_SHUTDOWN) != 0u;
}
