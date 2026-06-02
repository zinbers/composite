#pragma once
#include <cstdint>

class IpcBridge;

// SharedTexturePublisher owns the IpcBridge on the producer side.
// It receives pixel data from QmlOffscreenRenderer and forwards it via IPC.
class SharedTexturePublisher
{
public:
    SharedTexturePublisher();
    ~SharedTexturePublisher();

    bool initialize(int width, int height);

    // Copy pixels into shared memory and signal the consumer.
    void publishFrame(const uint8_t *pixels, int width, int height);

    // Write shutdown flag and wake the consumer.
    void shutdown();

    bool isReady() const { return m_ready; }

private:
    IpcBridge *m_ipc  = nullptr;
    bool       m_ready = false;
};
