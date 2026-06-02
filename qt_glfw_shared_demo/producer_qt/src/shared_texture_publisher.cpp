#include "shared_texture_publisher.h"
#include "ipc_bridge.h"
#include "log.h"

SharedTexturePublisher::SharedTexturePublisher()
    : m_ipc(new IpcBridge())
{}

SharedTexturePublisher::~SharedTexturePublisher()
{
    shutdown();
    delete m_ipc;
}

bool SharedTexturePublisher::initialize(int /*width*/, int /*height*/)
{
    if (!m_ipc->initProducer()) {
        LOG_ERROR("SharedTexturePublisher: IPC init failed");
        return false;
    }
    m_ready = true;
    return true;
}

void SharedTexturePublisher::publishFrame(const uint8_t *pixels,
                                           int width, int height)
{
    if (!m_ready) return;
    m_ipc->publishFrame(pixels,
                        static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height));
}

void SharedTexturePublisher::shutdown()
{
    if (m_ready) {
        m_ipc->signalShutdown();
        m_ready = false;
    }
}
