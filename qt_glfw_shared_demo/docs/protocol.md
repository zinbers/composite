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
