#pragma once
#include <cstdint>

// ─── DMA-BUF IPC socket path ─────────────────────────────────────────────────
static constexpr const char *DMABUF_SOCK_PATH = "/tmp/qt_glfw_dmabuf.sock";

// ─── Protocol identifiers ────────────────────────────────────────────────────
static constexpr uint32_t DMABUF_MAGIC   = 0x44424246u; // 'DBBF'
static constexpr uint32_t DMABUF_VERSION = 1u;

// ─── Flags ───────────────────────────────────────────────────────────────────
static constexpr uint32_t DMABUF_FLAG_SHUTDOWN = (1u << 0);
// Set on the first frame message (and whenever buffers are recreated):
// DMABUF_SLOT_COUNT file-descriptors are passed via SCM_RIGHTS in the same
// sendmsg call.
static constexpr uint32_t DMABUF_FLAG_NEW_BUF  = (1u << 1);

// ─── Double-buffer slot count ────────────────────────────────────────────────
static constexpr int DMABUF_SLOT_COUNT = 2;

// ─── DRM fourcc for GL_RGBA8 (R G B A bytes in memory, little-endian) ───────
// fourcc_code('A','B','2','4') = [31:0] A:B:G:R 8:8:8:8 little-endian,
// meaning byte-0 = R, byte-1 = G, byte-2 = B, byte-3 = A – matching
// GL_RGBA8 / GL_RGBA / GL_UNSIGNED_BYTE storage.
static constexpr uint32_t DRM_FMT_ABGR8888 = 0x34324241u;

// ─── Per-frame handshake message ─────────────────────────────────────────────
// Sent through the Unix domain socket for every rendered frame.
//
// When DMABUF_FLAG_NEW_BUF is set, exactly DMABUF_SLOT_COUNT file-descriptors
// are appended via SCM_RIGHTS ancillary data in the same sendmsg() call.
// Each fd refers to a distinct DMA-BUF object (one per double-buffer slot).
//
// On subsequent frames the fds remain valid; only buf_index changes to tell
// the consumer which slot was just written.
struct DmaBufFrameMsg {
    uint32_t magic;         // DMABUF_MAGIC
    uint32_t version;       // DMABUF_VERSION
    uint32_t width;         // frame width  (pixels)
    uint32_t height;        // frame height (pixels)
    uint32_t stride;        // bytes per row (same for both slots)
    uint32_t drm_format;    // DRM fourcc pixel format
    uint64_t modifier;      // DRM format modifier (0 = DRM_FORMAT_MOD_LINEAR)
    uint64_t frame_id;      // monotonically increasing frame counter
    uint64_t timestamp_ns;  // CLOCK_REALTIME nanoseconds when frame was published
    uint32_t flags;         // DMABUF_FLAG_* bitmask
    uint32_t buf_index;     // active buffer slot index (0 or 1)
};
