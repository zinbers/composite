#pragma once
#include <cstdint>
#include <cstddef>

// ─── POSIX IPC object names ──────────────────────────────────────────────────
static constexpr const char *SHM_NAME           = "/qt_glfw_demo";
static constexpr const char *SEM_NEW_FRAME       = "/qt_glfw_newframe";
static constexpr const char *SEM_CONSUMER_READY  = "/qt_glfw_consumer_rdy";
static constexpr const char *SEM_SHUTDOWN        = "/qt_glfw_shutdown";

// ─── Protocol constants ──────────────────────────────────────────────────────
static constexpr uint32_t PROTO_MAGIC   = 0x5154474Cu; // "QTGL"
static constexpr uint32_t PROTO_VERSION = 1u;

// ─── Flag bits in SharedHeader::flags ───────────────────────────────────────
static constexpr uint32_t FLAG_DATA_VALID = (1u << 0); // at least one frame written
static constexpr uint32_t FLAG_SHUTDOWN   = (1u << 1); // producer is exiting

// ─── Pixel buffer limits ─────────────────────────────────────────────────────
static constexpr uint32_t MAX_WIDTH  = 1920u;
static constexpr uint32_t MAX_HEIGHT = 1080u;
static constexpr uint32_t CHANNELS   = 4u;  // RGBA8

// ─── Shared memory layout ────────────────────────────────────────────────────
//
//  offset 0                       : SharedHeader
//  offset sizeof(SharedHeader)    : pixel buffer 0  (MAX_WIDTH * MAX_HEIGHT * 4)
//  offset + PIXEL_BUF_SIZE        : pixel buffer 1  (same size)
//
//  Double-buffer protocol:
//    Producer writes pixels into buffer[1 - header.writeBuffer],
//    then sets header.writeBuffer to that index, increments frameId,
//    sets FLAG_DATA_VALID, then posts semNewFrame.
//
//    Consumer waits on semNewFrame, reads header.writeBuffer to find
//    which buffer was last completed, then reads pixels from that buffer.
//
struct SharedHeader {
    uint32_t magic;         // PROTO_MAGIC
    uint32_t version;       // PROTO_VERSION
    uint32_t width;         // frame width  (pixels)
    uint32_t height;        // frame height (pixels)
    uint32_t pixelFormat;   // 0x8058 = GL_RGBA8
    uint32_t writeBuffer;   // index of the buffer last written (0 or 1)
    uint32_t producerPid;
    uint32_t flags;         // FLAG_DATA_VALID | FLAG_SHUTDOWN
    uint64_t frameId;       // monotonically increasing frame counter
    uint64_t timestampNs;   // nanoseconds since CLOCK_REALTIME epoch
};
// sizeof(SharedHeader) == 48 bytes

static constexpr size_t PIXEL_BUF_SIZE =
    static_cast<size_t>(MAX_WIDTH) * MAX_HEIGHT * CHANNELS; // ~8 MB
static constexpr size_t SHM_TOTAL_SIZE =
    sizeof(SharedHeader) + 2u * PIXEL_BUF_SIZE;             // ~16 MB
