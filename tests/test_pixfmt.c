/*
 * test_pixfmt.c -- Host unit tests for PixFmt_ReadRGB (pixfmt.c).
 *
 * Regression coverage for KI-008: sprite masks were built by reading pixels
 * as 8-bit CLUT indices, which produced an empty mask on 16/32-bit direct
 * GWorlds (iMac G5 at millions of colours) and made every sprite invisible.
 * These tests pin the per-depth reads that fix used, running on the host in
 * milliseconds -- the bug that took a hardware session to find would fail
 * here instantly.
 */
#include "pixfmt.h"
#include "test_util.h"

/* 8-bit indexed read via a small colour table. */
static void test_8bit_indexed(void)
{
    ColorTable ct;
    ColorTable *ctp = &ct;
    CTabHandle h = &ctp;
    unsigned char row[4];
    RGBColor out;

    printf("test_8bit_indexed\n");
    ct.ctSeed = 0; ct.ctFlags = 0; ct.ctSize = 3; /* indices 0..3 valid */
    ct.ctTable[0].rgb.red = 0xFFFF; ct.ctTable[0].rgb.green = 0xFFFF; ct.ctTable[0].rgb.blue = 0xFFFF;
    ct.ctTable[1].rgb.red = 0x0000; ct.ctTable[1].rgb.green = 0x0000; ct.ctTable[1].rgb.blue = 0x0000;
    ct.ctTable[2].rgb.red = 0xAAAA; ct.ctTable[2].rgb.green = 0xBBBB; ct.ctTable[2].rgb.blue = 0xCCCC;
    ct.ctTable[3].rgb.red = 0x1111; ct.ctTable[3].rgb.green = 0x2222; ct.ctTable[3].rgb.blue = 0x3333;

    row[0] = 2; row[1] = 1; row[2] = 0; row[3] = 99; /* 99 > ctSize -> clamps to 0 */

    PixFmt_ReadRGB(row, 0, 8, h, &out);
    CHECK_EQ(out.red, 0xAAAA, "8bit col0 red");
    CHECK_EQ(out.green, 0xBBBB, "8bit col0 green");
    CHECK_EQ(out.blue, 0xCCCC, "8bit col0 blue");

    PixFmt_ReadRGB(row, 1, 8, h, &out);
    CHECK_EQ(out.red, 0x0000, "8bit col1 black");

    /* Out-of-range index clamps to entry 0 (white), never reads past ctSize. */
    PixFmt_ReadRGB(row, 3, 8, h, &out);
    CHECK_EQ(out.red, 0xFFFF, "8bit oob index clamps to 0");
}

/* 16-bit 5-5-5 direct: 0 rrrrr ggggg bbbbb, big-endian. */
static void test_16bit_555(void)
{
    unsigned char row[6];
    RGBColor out;

    printf("test_16bit_555\n");

    /* white 0x7FFF */
    row[0] = 0x7F; row[1] = 0xFF;
    /* pure red 0x7C00 (r5=31,g=0,b=0) */
    row[2] = 0x7C; row[3] = 0x00;
    /* black 0x0000 */
    row[4] = 0x00; row[5] = 0x00;

    PixFmt_ReadRGB(row, 0, 16, (CTabHandle)0, &out);
    CHECK_EQ(out.red, 0xFFFF, "16bit white red");
    CHECK_EQ(out.green, 0xFFFF, "16bit white green");
    CHECK_EQ(out.blue, 0xFFFF, "16bit white blue");

    PixFmt_ReadRGB(row, 1, 16, (CTabHandle)0, &out);
    CHECK_EQ(out.red, 0xFFFF, "16bit red red");
    CHECK_EQ(out.green, 0x0000, "16bit red green");
    CHECK_EQ(out.blue, 0x0000, "16bit red blue");

    PixFmt_ReadRGB(row, 2, 16, (CTabHandle)0, &out);
    CHECK_EQ(out.red, 0x0000, "16bit black red");
}

/* 32-bit direct: [unused, R, G, B], big-endian. */
static void test_32bit_xrgb(void)
{
    unsigned char row[8];
    RGBColor out;

    printf("test_32bit_xrgb\n");

    /* col0: R=0x12 G=0x34 B=0x56 */
    row[0] = 0x00; row[1] = 0x12; row[2] = 0x34; row[3] = 0x56;
    /* col1: R=0xFF G=0x00 B=0x80 */
    row[4] = 0xFF; row[5] = 0xFF; row[6] = 0x00; row[7] = 0x80;

    PixFmt_ReadRGB(row, 0, 32, (CTabHandle)0, &out);
    CHECK_EQ(out.red, 0x1212, "32bit col0 red replicated");
    CHECK_EQ(out.green, 0x3434, "32bit col0 green replicated");
    CHECK_EQ(out.blue, 0x5656, "32bit col0 blue replicated");

    PixFmt_ReadRGB(row, 1, 32, (CTabHandle)0, &out);
    CHECK_EQ(out.red, 0xFFFF, "32bit col1 red");
    CHECK_EQ(out.green, 0x0000, "32bit col1 green");
    CHECK_EQ(out.blue, 0x8080, "32bit col1 blue");
}

/*
 * The actual KI-008 failure: on a 32-bit display, white and black pixels must
 * read as DIFFERENT colours. The old index-only reader collapsed both onto
 * ctTable[0], so the mask distance was zero everywhere -> empty mask ->
 * invisible sprite. A depth-aware read keeps them distinct.
 */
static void test_ki008_distinct_on_32bit(void)
{
    unsigned char whitePx[4];
    unsigned char blackPx[4];
    RGBColor w, b;
    long dr, dg, db;

    printf("test_ki008_distinct_on_32bit\n");
    whitePx[0] = 0x00; whitePx[1] = 0xFF; whitePx[2] = 0xFF; whitePx[3] = 0xFF;
    blackPx[0] = 0x00; blackPx[1] = 0x00; blackPx[2] = 0x00; blackPx[3] = 0x00;

    PixFmt_ReadRGB(whitePx, 0, 32, (CTabHandle)0, &w);
    PixFmt_ReadRGB(blackPx, 0, 32, (CTabHandle)0, &b);
    dr = (long)w.red - (long)b.red;
    dg = (long)w.green - (long)b.green;
    db = (long)w.blue - (long)b.blue;
    CHECK(dr * dr + dg * dg + db * db > 0,
          "white and black must differ on 32-bit (empty-mask regression)");
    CHECK_EQ(w.red, 0xFFFF, "white reads white on 32-bit");
    CHECK_EQ(b.red, 0x0000, "black reads black on 32-bit");
}

int main(void)
{
    printf("== test_pixfmt ==\n");
    test_8bit_indexed();
    test_16bit_555();
    test_32bit_xrgb();
    test_ki008_distinct_on_32bit();
    return TEST_RESULT();
}
