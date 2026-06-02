# IPC Protocol Specification

## Shared Memory (`/qt_glfw_demo`)

### Total size

```
sizeof(SharedHeader) + 2 × MAX_WIDTH × MAX_HEIGHT × 4 bytes ≈ 16 MB
```

### Layout

| Offset | Size | Description |
|--------|------|-------------|
| 0 | `sizeof(SharedHeader)` = 48 B | Protocol header |
| 48 | `MAX_WIDTH × MAX_HEIGHT × 4` ≈ 8 MB | Pixel buffer 0 |
| 48 + 8 MB | same | Pixel buffer 1 |

### `SharedHeader` fields

| Field | Type | Description |
|-------|------|-------------|
| `magic` | `uint32_t` | Fixed `0x5154474C` ("QTGL") – used for validation |
| `version` | `uint32_t` | `1` – increment on incompatible layout changes |
| `width` | `uint32_t` | Frame width in pixels |
| `height` | `uint32_t` | Frame height in pixels |
| `pixelFormat` | `uint32_t` | `0x8058` = `GL_RGBA8` |
| `writeBuffer` | `uint32_t` | Index (0 or 1) of the buffer last written by the producer |
| `producerPid` | `uint32_t` | PID of the producer process |
| `flags` | `uint32_t` | Bit 0: `FLAG_DATA_VALID`; Bit 1: `FLAG_SHUTDOWN` |
| `frameId` | `uint64_t` | Monotonically increasing counter; starts at 1 |
| `timestampNs` | `uint64_t` | `CLOCK_REALTIME` nanoseconds when the frame was published |

### Pixel format

Raw RGBA8 (4 bytes per pixel, row-major, **bottom-row first** – OpenGL convention from `glReadPixels`).  
The consumer flips the Y-axis in the GLSL shader.

---

## Double-buffer protocol

```
Producer                         Consumer
--------                         --------
writeBuf = 1 - header.writeBuffer
memcpy pixels → pixelBuf[writeBuf]
header.width/height/format = …
header.writeBuffer = writeBuf    ← publish atomically (last write)
header.frameId++
sem_post(semNewFrame)  ──────→   sem_timedwait(semNewFrame)
                                 readBuf = header.writeBuffer
                                 read pixelBuf[readBuf]
```

The producer always writes into the buffer **not** currently pointed to by `header.writeBuffer`.  Publishing the new `writeBuffer` index is the final write, acting as a logical store-release.  The semaphore post/wait provides the OS-level memory barrier.

---

## Named semaphores

| Name | Initial value | Meaning |
|------|---------------|---------|
| `/qt_glfw_newframe` | 0 | Producer posts once per rendered frame |
| `/qt_glfw_consumer_rdy` | 0 | Consumer posts once it is ready to receive |
| `/qt_glfw_shutdown` | 0 | Either side posts to signal orderly exit |

---

## Lifecycle

1. **Producer** calls `shm_open(O_CREAT)` and `sem_open(O_CREAT|O_EXCL)` – all three semaphores.  
   Any stale objects from a previous crashed run are removed first with `shm_unlink` / `sem_unlink`.

2. **Consumer** polls `shm_open(O_RDWR)` (no `O_CREAT`) until it succeeds, then opens the semaphores.

3. **Normal shutdown**: producer sets `FLAG_SHUTDOWN` in `flags`, posts both `semShutdown` and `semNewFrame` (to wake a blocked consumer).

4. **Crash recovery**: the producer removes all stale objects on startup.

---

## Compatibility

The consumer validates `magic` and `version` immediately after mapping.  If either field does not match, the consumer logs an error and disconnects.  Handle changes to `version` by bumping the constant in `ipc_protocol.h` in both processes simultaneously.

---

## DMA-BUF Zero-Copy Protocol (`--dmabuf` mode)

The DMA-BUF path eliminates all CPU copies by sharing GPU buffer objects directly between processes via Linux DMA-BUF file descriptors.

### Transport

A **Unix-domain socket** (`SOCK_SEQPACKET`) at path `/tmp/qt_glfw_dmabuf.sock` carries two kinds of messages:

| Condition | Payload |
|-----------|---------|
| First frame (`DMABUF_FLAG_NEW_BUF` set) | `DmaBufFrameMsg` + `DMABUF_SLOT_COUNT` (=2) DMA-BUF fds via `SCM_RIGHTS` |
| Subsequent frames | `DmaBufFrameMsg` only (no fds) |
| Shutdown (`DMABUF_FLAG_SHUTDOWN` set) | `DmaBufFrameMsg` with `width=height=0` |

`SOCK_SEQPACKET` is used (rather than `SOCK_STREAM`) to preserve message boundaries; each `sendmsg` / `recvmsg` call delivers exactly one `DmaBufFrameMsg`.

### `DmaBufFrameMsg` structure

| Field | Type | Description |
|-------|------|-------------|
| `magic` | `uint32_t` | `0x44424246` (`DBBF`) – validation guard |
| `version` | `uint32_t` | `1` |
| `width` | `uint32_t` | Frame width in pixels |
| `height` | `uint32_t` | Frame height in pixels |
| `stride` | `uint32_t` | Row stride in bytes |
| `drm_format` | `uint32_t` | DRM FourCC format code (e.g. `DRM_FMT_ABGR8888`) |
| `modifier` | `uint64_t` | DRM format modifier (e.g. `DRM_FORMAT_MOD_LINEAR = 0`) |
| `frame_id` | `uint64_t` | Monotonically increasing counter |
| `timestamp_ns` | `uint64_t` | `CLOCK_REALTIME` nanoseconds when published |
| `flags` | `uint32_t` | `DMABUF_FLAG_SHUTDOWN` (0x1, bit 0), `DMABUF_FLAG_NEW_BUF` (0x2, bit 1) |
| `buf_index` | `uint32_t` | Slot index (0 or 1) of the buffer just rendered into |

### Double-buffer slots

Both producer and consumer maintain **two** DMA-BUF slots.  The producer alternates between slot 0 and slot 1 each frame (`buf_index`).  The consumer imports each slot on first use and keeps the EGLImage/GL texture alive for subsequent re-use.

On the first frame the producer sends **both** DMA-BUF file descriptors (one per slot) via `SCM_RIGHTS` in the same `sendmsg` call as the `DmaBufFrameMsg`.  The consumer `dup()`s each received fd and imports it into an `EGLImage` via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)`.  Subsequent frame messages carry no fds; the consumer simply re-binds the already-imported GL texture identified by `buf_index`.

### Synchronization

The producer calls `glFinish()` before `publishFrame()`.  The Unix socket `sendmsg` / `recvmsg` call provides the necessary CPU-level memory barrier.  The consumer is safe to sample the texture immediately after `recvmsg` returns.

### Y-flip

DMA-BUF imported textures are in GPU-native orientation (top-row first) — **no Y-flip is needed**.  The GLSL shader `uFlipY` uniform is set to `false` in this path.  (The pixel-copy path sets it to `true` because `glReadPixels` returns bottom-row first.)

### Platform requirements

* Linux, Mesa (Wayland/EGL)  
* Producer: Qt must use EGL backend (`QT_QPA_PLATFORM=wayland`); requires `EGL_MESA_image_dma_buf_export`  
* Consumer: GLFW must be created with `GLFW_EGL_CONTEXT_API`; requires `EGL_EXT_image_dma_buf_import` and `GL_OES_EGL_image`

