#pragma once
#include "dmabuf_defs.h"

// DmaBufChannel implements cross-process DMA-BUF file-descriptor passing via a
// Unix domain socket (SOCK_SEQPACKET) with SCM_RIGHTS ancillary data.
//
// ── Producer role ──────────────────────────────────────────────────────────
//  1. Call listenAndAccept() once – creates the server socket at
//     DMABUF_SOCK_PATH and blocks until exactly one consumer connects.
//  2. Call sendFrame() each rendered frame.
//     Pass fds/numFds > 0 only when DMABUF_FLAG_NEW_BUF is set
//     (first frame or after buffer recreation).
//
// ── Consumer role ──────────────────────────────────────────────────────────
//  1. Call tryConnect() in a polling loop until it returns true.
//  2. Call recvFrame() to block-wait for the next frame message.
//     Received fds (if any) are placed in outFds[].

class DmaBufChannel {
public:
    DmaBufChannel();
    ~DmaBufChannel();

    DmaBufChannel(const DmaBufChannel &) = delete;
    DmaBufChannel &operator=(const DmaBufChannel &) = delete;

    // ── Producer ─────────────────────────────────────────────────────────────
    // Creates a Unix domain socket at DMABUF_SOCK_PATH and blocks until one
    // consumer connects.  Removes a stale socket file first.  Returns true on
    // success.
    bool listenAndAccept();

    // Sends frame metadata over the connected socket.  If numFds > 0, the fds
    // are passed as SCM_RIGHTS ancillary data in the same sendmsg() call.
    // Returns false on error or if not connected.
    bool sendFrame(const DmaBufFrameMsg &msg,
                   const int *fds = nullptr, int numFds = 0);

    // ── Consumer ─────────────────────────────────────────────────────────────
    // Single non-blocking connection attempt.  Returns false immediately when
    // the server socket is not yet available.
    bool tryConnect();

    // Blocking receive with a millisecond deadline.  Fills *msg on success.
    // Any received fds are placed into outFds[0..min(maxFds,received)-1];
    // remaining slots are set to -1.  Returns false on timeout or error.
    bool recvFrame(DmaBufFrameMsg &msg,
                   int *outFds = nullptr, int maxFds = 0,
                   int timeoutMs = 1000);

    bool isConnected() const { return m_sockFd >= 0; }

private:
    int m_listenFd = -1; // server socket  (producer only)
    int m_sockFd   = -1; // connected peer socket
};
