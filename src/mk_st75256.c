/**
 * mk_st75256.c  –  JLX160160G-948 / ST75161 driver (Pico SDK)
 *
 * Byte model (matches the working STM32 reference code):
 *   Column address 0-159  → one per pixel column
 *   Page   address 0-39   → one per four pixel rows
 *   Byte at (page P, col C) covers pixel rows P*4..P*4+3, column C
 *   Bits: [row_P | row_P+1 | row_P+2 | row_P+3] 2bpp each, MSB = top row
 *   After writing (col_end - col_start + 1) bytes the internal row
 *   pointer advances by 4 automatically.
 *
 * We keep a full 40-page framebuffer (6400 bytes) in RAM.
 * push_gb_line() writes one GB scanline into the buffer.
 * At line 143 (last scanline) the entire buffer is blasted out in
 * one contiguous SPI transfer — same approach as the STM32 RenderFrame().
 */

#include "mk_st75256.h"
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <string.h>

#define PIN_CS   17u
#define PIN_CLK  18u
#define PIN_SDA  19u
#define PIN_RS   20u
#define PIN_RST  21u
#define PIN_LED  22u
#define LCD_SPI  spi0

/* JLX160160G-948-PN manufacturer's electronic-volume setting.  Keep every
 * path that returns to full display mode on exactly the init value. */
#define LCD_VOP_LOW   0x1Du
#define LCD_VOP_HIGH  0x04u

/* ── SPI primitives ─────────────────────────────────────────────────── */
static inline void cs_lo(void)  { gpio_put(PIN_CS, 0); }
static inline void cs_hi(void)  { gpio_put(PIN_CS, 1); }
static inline void rs_cmd(void) { gpio_put(PIN_RS, 0); }
static inline void rs_dat(void) { gpio_put(PIN_RS, 1); }

static void wcmd(uint8_t c)
{ rs_cmd(); cs_lo(); spi_write_blocking(LCD_SPI,&c,1); cs_hi(); }

static void wdat(uint8_t d)
{ rs_dat(); cs_lo(); spi_write_blocking(LCD_SPI,&d,1); cs_hi(); }

static void wbuf(const uint8_t *b, size_t n)
{ rs_dat(); cs_lo(); spi_write_blocking(LCD_SPI,b,n); cs_hi(); }

/* ── Address window ──────────────────────────────────────────────────── */
/*
 * Set column window cs..ce and page window ps..pe, then enter write mode.
 * In 4-gray mode one page contains four pixel rows, so the full 160-pixel
 * panel uses page addresses 0..39 as shown by the manufacturer's example.
 */
static void set_win(uint8_t cs, uint8_t ce, uint8_t ps, uint8_t pe)
{
    wcmd(0x15); wdat(cs); wdat(ce);
    wcmd(0x75); wdat(ps); wdat(pe);
    wcmd(0x30);
    wcmd(0x5C);
}

/* ── Full-frame buffer ───────────────────────────────────────────────── */
/*
 * 40 pages × 160 cols = 6400 bytes.
 * Initialized to 0x00 = all gray-0 (white).
 * Margin pages (0-1 top, 38-39 bottom) are never written by
 * push_gb_line(), so they stay white automatically.
 */
static uint8_t framebuf[ST75256_PAGES][ST75256_COLS];

/* ── Init ───────────────────────────────────────────────────────────────── */

void mk_st75256_init(void)
{
    gpio_put(PIN_LED, 1);          /* backlight on */

    /* Recover the complete serial peripheral as well as the panel.  Core 1
     * can be reset during a failed/aborted transfer, leaving SPI0 enabled
     * with stale FIFO state even though the next display session is new. */
    cs_hi();
    rs_cmd();
    spi_deinit(LCD_SPI);
    gpio_set_function(PIN_CLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDA, GPIO_FUNC_SPI);
    spi_init(LCD_SPI, 30u * 1000u * 1000u);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    cs_hi();

    gpio_put(PIN_RST, 0); sleep_ms(100);
    gpio_put(PIN_RST, 1); sleep_ms(200);

    wcmd(0x30);                    /* EXT=0                               */
    wcmd(0x94);                    /* sleep out                           */

    wcmd(0x31);                    /* EXT=1                               */
    wcmd(0xD7); wdat(0x9F);        /* autoread disable                    */

    wcmd(0x32);                    /* analog set                          */
    wdat(0x00);                    /* oscillator frequency                */
    wdat(0x01);                    /* booster capacitor frequency = 6 kHz */
    wdat(0x00);                    /* bias = 1/14                          */

    /* 4-level gray table */
    wcmd(0x20);
    {
        static const uint8_t gt[16] = {
            0x01,0x03,0x05,0x07,
            0x09,0x0B,0x0D,0x10,
            0x11,0x13,0x15,0x17,
            0x19,0x1B,0x1D,0x1F
        };
        wbuf(gt, 16);
    }

    wcmd(0x31);                    /* EXT=1 - frame phase memory          */
    wcmd(0xF0);
    wdat(0x0F); wdat(0x0F); wdat(0x0F); wdat(0x0F);

    wcmd(0x30);                    /* EXT=0 from here                     */

    /* Manufacturer's initial maximum page/column ranges.  Drawing calls
     * below narrow the active window to exactly 160 columns x 40 pages. */
    wcmd(0x75); wdat(0x00); wdat(0x28);

    wcmd(0x15); wdat(0x00); wdat(0xFF);

    /* The panel is mounted 180 degrees in this device.  Reverse both scan
     * axes and the serial data direction while retaining the 948 timing. */
    wcmd(0xBC); wdat(0x03);
    wcmd(0x0C);
    wcmd(0xA6);

    /* Display control: duty=160 */
    wcmd(0xCA); wdat(0x00); wdat(0x9F); wdat(0x20);

    /* The manufacturer starts in monochrome mode (0x10); the emulator
     * deliberately selects its documented four-gray mode (0x11).
     * In this mode column=pixel-column (0-159), page=4-row group (0-39),
     * each byte = [row0|row1|row2|row3] packed 2bpp, MSB=top.           */
    wcmd(0xF0); wdat(0x11);

    /* Contrast */
    wcmd(0x81); wdat(LCD_VOP_LOW); wdat(LCD_VOP_HIGH);

    /* Power: regulator + follower + booster */
    wcmd(0x20); wdat(0x0B);

    sleep_ms(20);

    /* The controller RAM is undefined after power-on.  Clear the complete
     * 4-gray window while the display is still off, then enable scanning. */
    memset(framebuf, 0, sizeof(framebuf));
    set_win(0, (uint8_t)(ST75256_COLS - 1u),
            0, (uint8_t)(ST75256_PAGES - 1u));
    rs_dat();
    cs_lo();
    for (uint8_t p = 0; p < ST75256_PAGES; p++)
        spi_write_blocking(LCD_SPI, framebuf[p], ST75256_COLS);
    cs_hi();

    wcmd(0xAF);                    /* display ON                          */
}

/* ── Whole-screen fill ───────────────────────────────────────────────── */
void mk_st75256_fill(uint8_t gray)
{
    uint8_t b = mk_st75256_pack_v(gray, gray, gray, gray);
    memset(framebuf, b, sizeof(framebuf));

    /* Manufacturer's 4-gray example: 160 columns x 40 pages = 6400 B. */
    set_win(0, (uint8_t)(ST75256_COLS-1u),
            0, (uint8_t)(ST75256_PAGES-1u));
    rs_dat(); cs_lo();
    for (uint8_t p = 0; p < ST75256_PAGES; p++)
        spi_write_blocking(LCD_SPI, framebuf[p], ST75256_COLS);
    cs_hi();
}

/* ── GB scanline → framebuffer → flush at frame end ─────────────────── */
/*
 * gray_px[x] is 0 (white) … 3 (black), already the direct shade index
 * from peanut_gb's pixel bits [1:0].
 *
 * Byte layout: bits[7:6]=row0_top, [5:4]=row1, [3:2]=row2, [1:0]=row3_bot
 *
 * GB_Y_OFFSET=8 → GB rows 0-143 → display rows 8-151
 *              → framebuf pages 2-37 (page = display_row / 4)
 * Pages 0-1 (rows 0-7) and 38-39 (rows 152-159) stay 0x00 = white.
 */
void mk_st75256_push_gb_line(uint8_t gb_line, const uint8_t gray_px[GB_LCD_WIDTH])
{
    uint8_t display_row = (uint8_t)(gb_line + GB_Y_OFFSET);
    uint8_t page        = (uint8_t)(display_row / 4u);
    uint8_t row_slot    = (uint8_t)(display_row % 4u);  /* 0=top … 3=bot */
    uint8_t shift       = (uint8_t)((3u - row_slot) * 2u); /* MSB = top  */
    uint8_t mask        = (uint8_t)(0x03u << shift);

    for (uint16_t col = 0; col < ST75256_COLS; col++) {
        uint8_t gray = gray_px[col] & 3u;
        framebuf[page][col] =
            (uint8_t)((framebuf[page][col] & ~mask) | (uint8_t)(gray << shift));
    }

    /* Last GB scanline: flush entire framebuffer in one shot.
     * set_win once, stream 40×160 = 6400 bytes — same as STM32 RenderFrame. */
    if (gb_line == (uint8_t)(GB_LCD_HEIGHT - 1u)) {
        set_win(0, (uint8_t)(ST75256_COLS-1u),
                0, (uint8_t)(ST75256_PAGES-1u));
        rs_dat(); cs_lo();
        for (uint8_t p = 0; p < ST75256_PAGES; p++)
            spi_write_blocking(LCD_SPI, framebuf[p], ST75256_COLS);
        cs_hi();
    }
}

/* ── Display-control stub ────────────────────────────────────────────── */
void mk_st75256_display_control(bool invert, st75256_color_mode_e mode)
{
    (void)invert;
    (void)mode;

    /* The JLX160160G-948 example defines one calibrated Vop, so do not let a
     * runtime mode toggle silently alter the panel's analogue voltage. */
    wcmd(0x81);
    wdat(LCD_VOP_LOW);
    wdat(LCD_VOP_HIGH);
}

/* ── 8×8 font ────────────────────────────────────────────────────────── */
static const uint8_t FONT[][8] = {
    {0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, /* A */
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, /* B */
    {0x1E,0x30,0x60,0x60,0x60,0x30,0x1E,0x00}, /* C */
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, /* D */
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00}, /* E */
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, /* F */
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00}, /* G */
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, /* H */
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* I */
    {0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00}, /* J */
    {0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0x00}, /* K */
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, /* L */
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, /* M */
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, /* N */
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, /* O */
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, /* P */
    {0x78,0xCC,0xCC,0xCC,0xCC,0xDC,0x7E,0x00}, /* Q */
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, /* R */
    {0x3C,0x66,0x70,0x3C,0x0E,0x66,0x3C,0x00}, /* S */
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, /* T */
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, /* U */
    {0x66,0x66,0x66,0x66,0x3C,0x3C,0x18,0x00}, /* V */
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, /* W */
    {0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00}, /* X */
    {0xC3,0x66,0x3C,0x18,0x18,0x18,0x18,0x00}, /* Y */
    {0xFE,0x0C,0x18,0x30,0x60,0xC0,0xFE,0x00}, /* Z */
    {0x3C,0x66,0x6E,0x7E,0x76,0x66,0x3C,0x00}, /* 0 */
    {0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x00}, /* 1 */
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, /* 2 */
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, /* 3 */
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00}, /* 4 */
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, /* 5 */
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, /* 6 */
    {0x7E,0x06,0x06,0x0C,0x18,0x18,0x18,0x00}, /* 7 */
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, /* 8 */
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, /* 9 */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, /* - */
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, /* ( */
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, /* ) */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* . */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' ' */
};
static const char FONT_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-!(). ";

static void get_glyph(char c, uint8_t out[8])
{
    if (c>='a'&&c<='z') c=(char)(c-32);
    for (int i=0; FONT_CHARS[i]; i++)
        if (FONT_CHARS[i]==c) { memcpy(out,FONT[i],8); return; }
    memset(out, 0, 8);
}

/* ── Text ────────────────────────────────────────────────────────────── */
/*
 * Renders into the framebuffer then flushes only the affected pages.
 * x: pixel column (0-152, multiples of 8 ideal)
 * y: pixel row    (multiples of 4; each char = 8 rows = 2 pages)
 */
void mk_st75256_text(char *s, uint8_t x, uint8_t y, uint16_t fg565, uint16_t bg565)
{
    uint8_t fg = mk_st75256_rgb565_to_gray(fg565);
    uint8_t bg = mk_st75256_rgb565_to_gray(bg565);
    uint8_t glyph[8];
    uint8_t cx = x;
    y = (uint8_t)(y + 4u);

    for (size_t ci = 0; s[ci]; ci++) {
        if ((uint16_t)cx + 8u > ST75256_WIDTH) break;
        get_glyph(s[ci], glyph);

        /* Render 8 rows of the glyph into the framebuffer (2 pages of 4 rows) */
        for (uint8_t grp = 0; grp < 2u; grp++) {
            uint8_t font_row_base = (uint8_t)(grp * 4u);   /* 0 or 4       */
            uint8_t pixel_row     = (uint8_t)(y + grp*4u);
            uint8_t page          = (uint8_t)(pixel_row / 4u);
            if (page >= ST75256_PAGES) break;

            for (uint8_t fc = 0; fc < 8u; fc++) {
                uint8_t bit = (uint8_t)(7u - fc);
                uint8_t r0 = ((glyph[font_row_base+0]>>bit)&1u) ? fg : bg;
                uint8_t r1 = ((glyph[font_row_base+1]>>bit)&1u) ? fg : bg;
                uint8_t r2 = ((glyph[font_row_base+2]>>bit)&1u) ? fg : bg;
                uint8_t r3 = ((glyph[font_row_base+3]>>bit)&1u) ? fg : bg;
                framebuf[page][cx + fc] = mk_st75256_pack_v(r0, r1, r2, r3);
            }
        }

        /* Flush the 8 columns of this character.
         * Use full-height window (0-159) — same proven approach as the
         * game framebuffer flush. Sending all 40 pages x 8 cols = 320 B
         * per character avoids the page-vs-pixel-row ambiguity entirely. */
        set_win(cx, (uint8_t)(cx + 7u), 0, (uint8_t)(ST75256_PAGES - 1u));
        rs_dat(); cs_lo();
        for (uint8_t p = 0; p < ST75256_PAGES; p++)
            spi_write_blocking(LCD_SPI, &framebuf[p][cx], 8u);
        cs_hi();

        cx = (uint8_t)(cx + 8u);
    }
}
