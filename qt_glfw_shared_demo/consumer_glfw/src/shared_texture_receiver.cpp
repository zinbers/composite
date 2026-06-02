#include "shared_texture_receiver.h"
#include "ipc_bridge.h"
#include "log.h"

SharedTextureReceiver::SharedTextureReceiver()
    : m_ipc(new IpcBridge())
{}

SharedTextureReceiver::~SharedTextureReceiver()
{
    delete m_ipc;
}

bool SharedTextureReceiver::tryConnect()
{
    if (m_connected) return true;

    if (m_ipc->initConsumerOnce()) {
        m_connected = true;
        LOG_INFO("SharedTextureReceiver: connected to producer");
        return true;
    }
    return false;
}

void SharedTextureReceiver::signalReady()
{
    if (m_connected) m_ipc->signalConsumerReady();
}

bool SharedTextureReceiver::pollFrame(int timeoutMs)
{
    if (!m_connected) return false;
    return m_ipc->waitForFrame(timeoutMs);
}

const uint8_t *SharedTextureReceiver::pixels()  const { return m_ipc->framePixels(); }
int            SharedTextureReceiver::width()   const { return static_cast<int>(m_ipc->frameWidth());  }
int            SharedTextureReceiver::height()  const { return static_cast<int>(m_ipc->frameHeight()); }
uint64_t       SharedTextureReceiver::frameId() const { return m_ipc->frameId(); }
bool           SharedTextureReceiver::isShutdown() const { return m_ipc->isShutdown(); }
