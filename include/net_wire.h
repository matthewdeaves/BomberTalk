/*
 * net_wire.h -- On-the-wire byte order for network messages (portable core).
 *
 * BomberTalk's message structs are sent between machines. Multi-byte fields
 * must have one agreed byte order or a little-endian host (the Intel / Apple-
 * Silicon slice of the OS X .app) and a big-endian host (68k, PowerPC) would
 * read each other's shorts byte-swapped. The wire order is BIG-ENDIAN, chosen
 * because it matches the raw layout the classic Macs already send -- so this
 * seam is byte-identical to the historical protocol on every big-endian client
 * and only fixes the little-endian case. No protocol version bump.
 *
 * Only MsgPosition carries multi-byte fields (two shorts); every other message
 * is all single bytes and needs no conversion. The codec works on primitive
 * values, not the game.h structs, so it has no Toolbox dependency and is host
 * unit tested in tests/test_net_wire.c. It also reads/writes byte-by-byte, so
 * there is never an unaligned short access (68000-safe). See
 * notes/deepening-and-testability.md.
 */
#ifndef NET_WIRE_H
#define NET_WIRE_H

/* Big-endian 16-bit primitives. Host endianness does not matter: PutU16 always
 * writes the high byte first, GetU16 always reads it first. */
void NetWire_PutU16(unsigned char *p, unsigned short v);
unsigned short NetWire_GetU16(const unsigned char *p);

/* MsgPosition wire form: 8 bytes -- playerID, facing, pixelX(BE16),
 * pixelY(BE16), pad[2]=0. pixelX/pixelY are signed shorts carried as 16-bit
 * two's complement and round-trip exactly. */
#define NETWIRE_POSITION_LEN 8

void NetWire_PackPosition(unsigned char playerID, unsigned char facing,
                          short pixelX, short pixelY, unsigned char *out);
void NetWire_UnpackPosition(const unsigned char *in, unsigned char *playerID,
                            unsigned char *facing, short *pixelX, short *pixelY);

#endif /* NET_WIRE_H */
