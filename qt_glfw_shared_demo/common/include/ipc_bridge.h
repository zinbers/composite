#pragma once
#include <cstdint>

// IpcBridge wraps POSIX shared memory + named semaphores for cross-process
// pixel-frame exchange between producer_qt and consumer_glfw.
//
// Lifetime:
//   Producer: call initProducer() once, then publishFrame() each frame,
//             finally signalShutdown() before exit.
//   Consumer: call initConsumerOnce() in a retry loop until it returns true,
//             then signalConsumerReady(), then waitForFrame() / framePixels().

class IpcBridge
{
public:
    IpcBridge();
    ~IpcBridge();

    // Disable copy
    IpcBridge(const IpcBridge &) = delete;
    IpcBridge &operator=(const IpcBridge &) = delete;

    // ── Producer ─────────────────────────────────────────────────────────────
    // Creates shared memory and semaphores. Removes stale objects first.
    bool initProducer();

    // Copies pixels into the inactive buffer, updates header, posts semNewFrame.
    void publishFrame(const uint8_t *pixels, uint32_t width, uint32_t height);

    // Sets FLAG_SHUTDOWN and wakes the consumer.
    void signalShutdown();

    // ── Consumer ─────────────────────────────────────────────────────────────
    // Tries once to open the shared memory/semaphores. Returns false immediately
    // if the producer hasn't created them yet (no retry).
    bool initConsumerOnce();

    // Blocking wait with retry loop – convenience wrapper around initConsumerOnce.
    bool initConsumer(int timeoutSeconds = 30);

    // Posts semConsumerReady to let the producer know we are up.
    void signalConsumerReady();

    // Waits up to timeoutMs for semNewFrame. Returns true when a frame arrived.
    bool waitForFrame(int timeoutMs = 1000);

    // Pointer to pixels in the last-written buffer (valid until next waitForFrame).
    const uint8_t *framePixels() const;

    uint32_t frameWidth()  const;
    uint32_t frameHeight() const;
    uint64_t frameId()     const;
    bool     isShutdown()  const;

    bool isValid() const { return m_valid; }

private:
    struct Impl;
    Impl *m_impl  = nullptr;
    bool  m_valid = false;
};
