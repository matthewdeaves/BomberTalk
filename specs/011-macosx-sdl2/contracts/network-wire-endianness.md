# Contract: Network Wire Endianness

BomberTalk message payloads are exchanged across big-endian (68k/PPC Classic, PPC OS X 10.3–10.5) and little-endian (Intel OS X 10.7, arm64 macOS 26, Linux) hosts over the shared PeerTalk wire. This contract fixes the byte order so all eras agree.

## Canonical order: BIG-ENDIAN (network order)

Every multi-byte field is transmitted most-significant-byte-first. This matches the Classic Mac's native order, so the three validated classic builds emit byte-identical bytes with zero conversion cost.

## Conversion surface (exhaustive)

Only `MsgPosition` carries multi-byte fields:

| Field | Type | Direction |
|---|---|---|
| `MsgPosition.pixelX` | int16 | swap on encode (send) and decode (receive) on little-endian builds |
| `MsgPosition.pixelY` | int16 | swap on encode and decode on little-endian builds |

**All other fields on all other messages are single-byte** (`unsigned char`) and are transmitted verbatim on every architecture. `MSG_GAME_START.version` is a single byte (it reuses the old v1.0-alpha `pad` byte) — endian-neutral.

## Required behavior

1. **Encode (send)**: application fills native-order struct → wire encoder writes big-endian bytes. On big-endian hosts this is a copy (no-op swap); on little-endian hosts the two `MsgPosition` shorts are byte-swapped.
2. **Decode (receive)**: wire bytes (big-endian) → native-order struct fields before game logic reads them. Same no-op/swap split.
3. **No protocol version bump**: the on-wire byte *layout and size* are unchanged; only little-endian host interpretation changes. `BT_PROTOCOL_VERSION` is untouched.
4. **Big-endian output is byte-identical to today** (SC-004): verified by diffing on-wire bytes for a fixed message set on a classic build before/after.

## Implementation shape (`include/net_wire.h`)

- Provide `bt_hton16`/`bt_ntoh16` (and 32 if ever needed) that compile to no-ops on big-endian and byte-swaps on little-endian, selected by a build-time endianness define (not runtime detection in the hot path).
- `net.c` `Net_SendPosition` and the `on_position` receive handler route `pixelX`/`pixelY` through these. No other sender/handler changes.

## Test contract (Linux, little-endian)

- **Round-trip**: encode a `MsgPosition` with known `pixelX`/`pixelY` → decode → fields equal originals.
- **Fixed vector**: a hand-written big-endian byte array decodes to the expected `pixelX`/`pixelY` (proves we match the classic sender's bytes, not just self-consistency).
- **Hardware (deferred)**: LE build ↔ big-endian Classic Mac exchange positions; players map to the same tile (no false kills / no out-of-bounds).
