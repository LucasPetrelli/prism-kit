# Protocol

## Description

Simple TLV (Tag-Length-Value) protocol for command/data serialization over a
byte-stream transport (USB CDC ACM, UART, etc.).

## Wire Format

Each frame on the wire has the following layout:

| Field    | Length (Bytes) | Description                                    |
| -------- | -------------- | ---------------------------------------------- |
| Sync     | 1              | `0xAA` — frame start marker                    |
| Tag      | 2              | Command tag, little-endian                     |
| Length   | 2              | Data payload length in bytes, little-endian    |
| Data     | Length         | Payload (may be zero bytes)                    |
| Checksum | 1              | XOR of unescaped Tag + Length + Data           |

### Escaping

Within the Tag, Length, Data, and Checksum fields, two byte values are
reserved and must be escaped on the wire:

| Original | Wire Encoding | Notes                        |
| -------- | ------------- | ---------------------------- |
| `0xAA`   | `0xBB 0xAB`  | Sync byte                    |
| `0xBB`   | `0xBB 0xBA`  | Escape byte                  |

General rule: any `0xAA` or `0xBB` in the payload (after the sync byte but
before the end of the frame) is transmitted as `0xBB` followed by
`original ^ 0x01`. The leading sync byte is never escaped.

The receiver reverses the process: a `0xBB` byte signals that the next byte
must be XORed with `0x01` to recover the original value.

### Checksum

The checksum is the XOR of all **unescaped** bytes in the Tag (2 bytes),
Length (2 bytes), and Data (Length bytes). The checksum byte itself is
transmitted escaped if necessary.

If the checksum does not match on reception, the frame is discarded and the
parser re-synchronises.

### Constants

| Symbol                | Value   | Description                       |
| --------------------- | ------- | --------------------------------- |
| `kSyncByte`           | `0xAA`  | Frame start marker                |
| `kEscapeByte`         | `0xBB`  | Escape prefix                     |
| `kEscapeXor`          | `0x01`  | XOR mask for escaped bytes        |
| `kHeaderSize`         | 4       | Tag + Length                      |
| `kChecksumSize`       | 1       |                                   |
| `kMaxFrameDataLength` | 256     | Max data payload bytes            |
| `kMaxHandlers`        | 8       | Max registered `FrameHandler`s    |

## Tags

Tags are `uint16_t` values, little-endian on the wire.

| Tag       | Value    | Description                                |
| --------- | -------- | ------------------------------------------ |
| Loopback  | `0x0000` | Echo received data back (always enabled)   |
| Reserved  | `0x0000` – `0x00FF` | Protocol-level commands          |
| User      | `0x0100` – `0xFFFF` | Application-defined commands    |

## Main Files

 - [tags.hpp](include/tags.hpp) — `enum class Tag`.
 - [frame.hpp](include/frame.hpp) — Frame constants, `FrameHeader` struct,
   `Frame` class for construction and inspection.
 - [protocol.hpp](include/protocol.hpp) — `Protocol` class declaration with
   type aliases for stream callbacks and frame handlers.
 - [src/protocol.cpp](src/protocol.cpp) — Protocol implementation including
   the parser state machine and built-in loopback handler.

## Type Aliases

```cpp
using StreamReader  = uint32_t (*)(uint8_t* buffer, uint32_t length);
using StreamWriter  = bool (*)(const uint8_t* data, uint32_t length);
using FrameHandler  = void (*)(void* context, const uint8_t* data,
                                uint16_t length);
```

- `StreamReader` — non-blocking read from the transport; returns the number
  of bytes actually read (0 if none available).
- `StreamWriter` — non-blocking write to the transport; returns true if all
  bytes were written.
- `FrameHandler` — invoked when a frame with a registered tag is fully
  received. Receives a pointer to the payload data and its length. Returns
  true if the frame was consumed, false to drop it.

## Protocol API

A `Protocol` instance is constructed with a `ProtocolConfig` containing the
two stream callbacks:

```cpp
ProtocolConfig cfg{my_read, my_write};
Protocol proto(cfg);
```

| Method                                            | Description                                      |
| ------------------------------------------------- | ------------------------------------------------ |
| `add_handler(Tag, FrameHandler) → bool`              | Register handler for a tag. Fails if table full or tag duplicate. Protocol passes `this` as the handler's context. |
| `send(const Frame&) → bool`                       | Queue a frame for transmission. Fails if TX busy or frame invalid. |
| `run() → void`                                    | Run one iteration of RX/TX processing.           |
| `feed(const uint8_t*, uint32_t) → void`           | Push received bytes into the parser (alternative to `StreamReader`). |

## State Machine

`run()` cycles through these parser states:

```
kWaitingForSync ──[0xAA]──▶ kReadingHeader ──[4 bytes]──▶ kReadingData
      ▲                         │      ▲                    │
      │                         │      │                    │
      └────[error / resync]─────┘      │                    │
                                       │              [length bytes]
                                       │                    │
                              kDispatching ◀──[OK]── kReadingChecksum
```

- **kWaitingForSync**: scan for `0xAA`. All other bytes are discarded.
- **kReadingHeader**: accumulate 4 header bytes (unescaped). Validate
  `length ≤ kMaxFrameDataLength`. If invalid, reset.
- **kReadingData**: accumulate `length` data bytes (unescaped).
- **kReadingChecksum**: read 1 checksum byte. If it matches the computed
  XOR, transition to dispatching; otherwise reset.
- **kDispatching**: find a registered handler for the tag and invoke it.
  If the tag is `kLoopback` and loopback is enabled, echo the frame back.

### Resynchronisation

If `0xAA` (sync byte) appears mid-frame (during header, data, or checksum
reading), the parser resets to `kReadingHeader` and treats it as the start
of a new frame. This allows recovery after a byte slip without waiting for
a timeout.

## Thread Safety

`Protocol` is **not reentrant**. All calls must come from a single task or
be externally serialised. The typical pattern is to call `run()` from the
main application loop and `send()` from frame handlers invoked by `run()`.

## Process

 1. User code creates a `Protocol` instance with stream read/write callbacks.
 2. User code registers handlers for each tag the project wants to handle.
 3. User code periodically calls `run()` to read new frames, dispatch them,
    and transmit queued outgoing frames.
 4. User code calls `send()` to queue frames for transmission.
 5. Frame handlers may call `send()` to respond to received commands.

## Loopback Command (Tag 0x0000)

The loopback command is registered as the first entry in the handler table
at construction time. Any received frame with `Tag::kLoopback` is echoed
back with the same tag and identical data. This validates the entire
serialisation, transport, and deserialisation pipeline end-to-end.
No explicit registration is needed.

