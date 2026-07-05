/*
 * net_wire.c -- Big-endian byte order for network messages. See net_wire.h.
 *
 * Introduced 011-macosx-sdl2 (D3) to make the wire format explicit rather than
 * "both ends happen to be big-endian" -- the prerequisite for a little-endian
 * client (Intel / Apple-Silicon) to interoperate with the classic Macs.
 */
#include "net_wire.h"

void NetWire_PutU16(unsigned char *p, unsigned short v)
{
    p[0] = (unsigned char)((v >> 8) & 0xFF);
    p[1] = (unsigned char)(v & 0xFF);
}

unsigned short NetWire_GetU16(const unsigned char *p)
{
    return (unsigned short)(((unsigned short)p[0] << 8) | (unsigned short)p[1]);
}

void NetWire_PackPosition(unsigned char playerID, unsigned char facing,
                          short pixelX, short pixelY, unsigned char *out)
{
    out[0] = playerID;
    out[1] = facing;
    NetWire_PutU16(out + 2, (unsigned short)pixelX);
    NetWire_PutU16(out + 4, (unsigned short)pixelY);
    out[6] = 0;
    out[7] = 0;
}

void NetWire_UnpackPosition(const unsigned char *in, unsigned char *playerID,
                            unsigned char *facing, short *pixelX, short *pixelY)
{
    *playerID = in[0];
    *facing   = in[1];
    /* u16 two's complement -> short. Every target is two's complement (the
     * whole protocol already assumes it), so the cast reproduces the signed
     * value exactly, including negatives. */
    *pixelX = (short)NetWire_GetU16(in + 2);
    *pixelY = (short)NetWire_GetU16(in + 4);
}
