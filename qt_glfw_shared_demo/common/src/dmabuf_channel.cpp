#include "dmabuf_channel.h"
#include "log.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <vector>

// ─── Ctor / Dtor ─────────────────────────────────────────────────────────────
DmaBufChannel::DmaBufChannel()  = default;

DmaBufChannel::~DmaBufChannel()
{
    if (m_sockFd   >= 0) { close(m_sockFd);   m_sockFd   = -1; }
    if (m_listenFd >= 0) { close(m_listenFd); m_listenFd = -1; }
    // Remove the socket file so the next producer can start cleanly.
    unlink(DMABUF_SOCK_PATH);
}

// ─── Producer: listenAndAccept ───────────────────────────────────────────────
bool DmaBufChannel::listenAndAccept()
{
    // Remove stale socket from a previous crashed run.
    unlink(DMABUF_SOCK_PATH);

    m_listenFd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (m_listenFd < 0) {
        LOG_ERROR("DmaBufChannel: socket() failed: %s", strerror(errno));
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, DMABUF_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(m_listenFd, reinterpret_cast<struct sockaddr *>(&addr),
             sizeof(addr)) < 0) {
        LOG_ERROR("DmaBufChannel: bind() failed: %s", strerror(errno));
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    if (listen(m_listenFd, 1) < 0) {
        LOG_ERROR("DmaBufChannel: listen() failed: %s", strerror(errno));
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    LOG_INFO("DmaBufChannel: waiting for consumer to connect on %s …",
             DMABUF_SOCK_PATH);
    m_sockFd = accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
    if (m_sockFd < 0) {
        LOG_ERROR("DmaBufChannel: accept4() failed: %s", strerror(errno));
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    LOG_INFO("DmaBufChannel: consumer connected");
    return true;
}

// ─── Producer: sendFrame ─────────────────────────────────────────────────────
bool DmaBufChannel::sendFrame(const DmaBufFrameMsg &msg,
                               const int *fds, int numFds)
{
    if (m_sockFd < 0) return false;

    struct iovec iov{};
    iov.iov_base = const_cast<DmaBufFrameMsg *>(&msg);
    iov.iov_len  = sizeof(msg);

    struct msghdr msgh{};
    msgh.msg_iov    = &iov;
    msgh.msg_iovlen = 1;

    // Ancillary data for SCM_RIGHTS (only when fds are provided)
    std::vector<char> ctrlBuf;
    if (numFds > 0 && fds != nullptr) {
        ctrlBuf.resize(CMSG_SPACE(static_cast<size_t>(numFds) * sizeof(int)), 0);
        msgh.msg_control    = ctrlBuf.data();
        msgh.msg_controllen = ctrlBuf.size();

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type  = SCM_RIGHTS;
        cmsg->cmsg_len   = CMSG_LEN(static_cast<size_t>(numFds) * sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), fds,
                    static_cast<size_t>(numFds) * sizeof(int));
    }

    ssize_t n = sendmsg(m_sockFd, &msgh, MSG_NOSIGNAL);
    if (n < 0) {
        LOG_ERROR("DmaBufChannel: sendmsg() failed: %s", strerror(errno));
        return false;
    }
    return true;
}

// ─── Consumer: tryConnect ────────────────────────────────────────────────────
bool DmaBufChannel::tryConnect()
{
    if (m_sockFd >= 0) return true; // already connected

    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, DMABUF_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr),
                sizeof(addr)) < 0) {
        close(fd);
        return false; // server not yet up
    }

    m_sockFd = fd;
    LOG_INFO("DmaBufChannel: connected to producer at %s", DMABUF_SOCK_PATH);
    return true;
}

// ─── Consumer: recvFrame ─────────────────────────────────────────────────────
bool DmaBufChannel::recvFrame(DmaBufFrameMsg &msg,
                               int *outFds, int maxFds,
                               int timeoutMs)
{
    if (m_sockFd < 0) return false;

    // Poll with timeout
    struct pollfd pfd{ m_sockFd, POLLIN, 0 };
    int rc = poll(&pfd, 1, timeoutMs);
    if (rc <= 0) {
        if (rc < 0 && errno != EINTR)
            LOG_WARN("DmaBufChannel: poll() failed: %s", strerror(errno));
        return false;
    }

    // Ancillary data buffer large enough for DMABUF_SLOT_COUNT fds
    constexpr int kMaxFds = DMABUF_SLOT_COUNT;
    char ctrlBuf[CMSG_SPACE(kMaxFds * sizeof(int))];

    struct iovec iov{};
    iov.iov_base = &msg;
    iov.iov_len  = sizeof(msg);

    struct msghdr msgh{};
    msgh.msg_iov         = &iov;
    msgh.msg_iovlen      = 1;
    msgh.msg_control     = ctrlBuf;
    msgh.msg_controllen  = sizeof(ctrlBuf);

    ssize_t n = recvmsg(m_sockFd, &msgh, 0);
    if (n <= 0) {
        if (n < 0)
            LOG_ERROR("DmaBufChannel: recvmsg() failed: %s", strerror(errno));
        return false;
    }
    if (static_cast<size_t>(n) < sizeof(msg)) {
        LOG_ERROR("DmaBufChannel: short read (%zd / %zu bytes)",
                  n, sizeof(msg));
        return false;
    }

    // Initialise caller fd slots to "not received"
    for (int i = 0; i < maxFds; ++i)
        outFds[i] = -1;

    // Extract SCM_RIGHTS fds from ancillary data
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
         cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msgh, cmsg))
    {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
            continue;

        int nfds = static_cast<int>(
            (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        const int *rcvFds = reinterpret_cast<const int *>(CMSG_DATA(cmsg));

        for (int i = 0; i < nfds; ++i) {
            if (outFds && i < maxFds)
                outFds[i] = rcvFds[i];
            else
                close(rcvFds[i]); // discard excess fds to avoid leaking
        }
        break; // only one SCM_RIGHTS record expected
    }

    return true;
}
