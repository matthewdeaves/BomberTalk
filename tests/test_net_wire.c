/*
 * test_net_wire.c -- Host unit tests for the wire byte order (net_wire.c).
 *
 * The wire format is big-endian. These tests run on a little-endian host
 * (x86 Linux CI), so asserting the high byte comes first proves the codec is
 * host-endian-independent -- which is the whole point: a little-endian client
 * must produce the same bytes a big-endian classic Mac does. Also pins the
 * two's-complement round-trip for negative coordinates. (011 D3)
 */
#include "net_wire.h"
#include "test_util.h"

static void test_u16_big_endian(void)
{
    unsigned char buf[2];
    printf("test_u16_big_endian\n");
    NetWire_PutU16(buf, 0x1234);
    CHECK_EQ(buf[0], 0x12, "high byte written first");
    CHECK_EQ(buf[1], 0x34, "low byte written second");
    CHECK_EQ(NetWire_GetU16(buf), 0x1234, "round-trip u16");

    NetWire_PutU16(buf, 0x00FF);
    CHECK_EQ(buf[0], 0x00, "0x00FF high byte");
    CHECK_EQ(buf[1], 0xFF, "0x00FF low byte");

    NetWire_PutU16(buf, 0xFF00);
    CHECK_EQ(buf[0], 0xFF, "0xFF00 high byte");
    CHECK_EQ(buf[1], 0x00, "0xFF00 low byte");
}

static void test_position_layout(void)
{
    unsigned char buf[NETWIRE_POSITION_LEN];
    printf("test_position_layout\n");
    NetWire_PackPosition(1, 2, 0x0102, 0x0304, buf);
    CHECK_EQ(buf[0], 1, "byte0 = playerID");
    CHECK_EQ(buf[1], 2, "byte1 = facing");
    CHECK_EQ(buf[2], 0x01, "byte2 = pixelX high");
    CHECK_EQ(buf[3], 0x02, "byte3 = pixelX low");
    CHECK_EQ(buf[4], 0x03, "byte4 = pixelY high");
    CHECK_EQ(buf[5], 0x04, "byte5 = pixelY low");
    CHECK_EQ(buf[6], 0, "byte6 = pad 0");
    CHECK_EQ(buf[7], 0, "byte7 = pad 0");
}

static void roundtrip(unsigned char pid, unsigned char facing,
                      short x, short y)
{
    unsigned char buf[NETWIRE_POSITION_LEN];
    unsigned char gp, gf;
    short gx, gy;
    NetWire_PackPosition(pid, facing, x, y, buf);
    NetWire_UnpackPosition(buf, &gp, &gf, &gx, &gy);
    CHECK_EQ(gp, pid, "playerID round-trip");
    CHECK_EQ(gf, facing, "facing round-trip");
    CHECK_EQ(gx, x, "pixelX round-trip");
    CHECK_EQ(gy, y, "pixelY round-trip");
}

static void test_position_roundtrip(void)
{
    printf("test_position_roundtrip\n");
    roundtrip(0, 0, 0, 0);
    roundtrip(3, 4, 100, 200);
    roundtrip(2, 1, 4096, 3200);       /* typical tile-independent coords */
    roundtrip(1, 2, 32767, 32767);     /* SHRT_MAX */
}

static void test_negative_twos_complement(void)
{
    unsigned char buf[NETWIRE_POSITION_LEN];
    unsigned char gp, gf;
    short gx, gy;
    printf("test_negative_twos_complement\n");

    /* -1 must be 0xFFFF on the wire and decode back to -1. */
    NetWire_PackPosition(0, 0, -1, -50, buf);
    CHECK_EQ(buf[2], 0xFF, "-1 high byte");
    CHECK_EQ(buf[3], 0xFF, "-1 low byte");
    CHECK_EQ(buf[4], 0xFF, "-50 high byte");
    CHECK_EQ(buf[5], 0xCE, "-50 low byte (0xFFCE)");
    NetWire_UnpackPosition(buf, &gp, &gf, &gx, &gy);
    CHECK_EQ(gx, -1, "-1 decodes back");
    CHECK_EQ(gy, -50, "-50 decodes back");

    /* SHRT_MIN edge. */
    roundtrip(0, 0, -32768, -32768);
}

/*
 * Regression: MSG_GAME_START's numPlayers byte went straight into
 * gExpectedPlayers -> enter_game -> gGame.numPlayers, which Game_Init and the
 * game loop use as a bound on gGame.players[MAX_PLAYERS]. 0xFF (or 5..255, or
 * 0) from the wire indexed far past the array.
 */
static void test_roster_and_slot_validation(void)
{
    int n;
    printf("test_roster_and_slot_validation\n");
    CHECK_EQ(NetWire_ValidRoster(0), 0, "roster 0 invalid");
    CHECK_EQ(NetWire_ValidRoster(-1), 0, "roster -1 invalid");
    for (n = 1; n <= MAX_PLAYERS; n++)
        CHECK_EQ(NetWire_ValidRoster(n), 1, "roster 1..MAX_PLAYERS valid");
    CHECK_EQ(NetWire_ValidRoster(MAX_PLAYERS + 1), 0, "roster MAX+1 invalid");
    CHECK_EQ(NetWire_ValidRoster(255), 0, "roster 255 invalid");

    CHECK_EQ(NetWire_ValidSlot(-1), 0, "slot -1 invalid");
    for (n = 0; n < MAX_PLAYERS; n++)
        CHECK_EQ(NetWire_ValidSlot(n), 1, "slot 0..MAX_PLAYERS-1 valid");
    CHECK_EQ(NetWire_ValidSlot(MAX_PLAYERS), 0, "slot MAX invalid");
    CHECK_EQ(NetWire_ValidSlot(255), 0, "slot 255 invalid");
}

static void test_seat_roster(void)
{
    int n;
    printf("test_seat_roster\n");
    CHECK_EQ(NetWire_SeatRoster(4, 2), 4, "roster covering our slot is unchanged");
    CHECK_EQ(NetWire_SeatRoster(3, 2), 3, "slot == n-1 unchanged");
    CHECK_EQ(NetWire_SeatRoster(2, 2), 3, "rank 2, roster 2: widened to seat us");
    CHECK_EQ(NetWire_SeatRoster(1, 3), 4, "rank 3, roster 1: widened to 4");
    CHECK_EQ(NetWire_SeatRoster(0, 0), 1, "roster 0 raised to 1");
    CHECK_EQ(NetWire_SeatRoster(-5, 0), 1, "negative roster raised to 1");
    CHECK_EQ(NetWire_SeatRoster(99, 0), MAX_PLAYERS, "oversized roster clamped");
    for (n = -2; n < 12; n++) {
        int id;
        for (id = 0; id < MAX_PLAYERS; id++) {
            int r = NetWire_SeatRoster(n, id);
            CHECK(NetWire_ValidRoster(r) && r > id,
                  "result is a valid roster that includes our slot");
        }
    }
}

int main(void)
{
    printf("== test_net_wire ==\n");
    test_u16_big_endian();
    test_position_layout();
    test_position_roundtrip();
    test_negative_twos_complement();
    test_roster_and_slot_validation();
    test_seat_roster();
    return TEST_RESULT();
}
