#pragma once
#include <cstdint>

class IpcBridge;

// SharedTextureReceiver owns the consumer-side IpcBridge.
// It tries to connect to the producer non-blockingly and polls for frames.
class SharedTextureReceiver
{
public:
    SharedTextureReceiver();
    ~SharedTextureReceiver();

    // Single non-blocking connect attempt.
    // Returns true when the shared memory and semaphores are available.
    bool tryConnect();

    // Wait up to timeoutMs for the next frame.
    // Returns true when a new frame arrived.
    bool pollFrame(int timeoutMs = 16);

    // Pixel data of the last received frame (valid until the next pollFrame call).
    const uint8_t *pixels()  const;
    int            width()   const;
    int            height()  const;
    uint64_t       frameId() const;

    bool isConnected()  const { return m_connected; }
    bool isShutdown()   const;

    // Notify the producer that the consumer is ready.
    void signalReady();

private:
    IpcBridge *m_ipc       = nullptr;
    bool       m_connected = false;
};
