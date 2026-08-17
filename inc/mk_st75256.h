/**
 * mk_st75256.h  –  JLX160160G-948 / ST75161 driver for Raspberry Pi Pico
 *
 * Byte model (matches working STM32 reference code):
 *   Column address 0-159  → one address per pixel column
 *   Page   address 0-39   → one address per four pixel rows
 *   Each byte at (page P, col C) covers rows 4P..4P+3 at column C:
 *       bits [7:6] = row 4P   (top)
 *       bits [5:4] = row 4P+1
 *       bits [3:2] = row 4P+2
 *       bits [1:0] = row 4P+3 (bottom)
 *   Gray 0 = white … gray 3 = black.
 *
 * A 6400-byte framebuffer (40 pages × 160 cols) is kept in RAM.
 * The entire buffer is flushed in one SPI burst at the end of each
 * GB frame (line 143), mirroring STM32's single-shot RenderFrame().
 *
 * Wiring (matches Pico-GB):
 *   GPIO 17 = CS    GPIO 18 = CLK (SPI0)    GPIO 19 = MOSI (SPI0)
 *   GPIO 20 = RS    GPIO 21 = RST           GPIO 22 = LED (backlight)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Display geometry */
#define ST75256_WIDTH    160u
#define ST75256_HEIGHT   160u
#define ST75256_COLS     160u   /* column addresses (1 per pixel column) */
#define ST75256_PAGES     40u   /* page groups      (4 pixel rows each)  */

/* Game Boy inside 160×160: 8-row top/bottom margin */
#define GB_LCD_WIDTH     160u
#define GB_LCD_HEIGHT    144u
#define GB_Y_OFFSET        8u  /* must be a multiple of 4               */

typedef enum {
    ST75256_COLOR_MODE_FULL = 0,
    ST75256_COLOR_MODE_IDLE = 1
} st75256_color_mode_e;

/* ── Public API ──────────────────────────────────────────────────────── */
void mk_st75256_init(void);
void mk_st75256_fill(uint8_t gray);

/**
 * Write one GB scanline (0-143) into the framebuffer.
 * gray_px[x] = 0 (white) … 3 (black) — the raw peanut_gb shade index.
 * When gb_line == 143 the full framebuffer is flushed to the display.
 */
void mk_st75256_push_gb_line(uint8_t gb_line, const uint8_t gray_px[GB_LCD_WIDTH]);

void mk_st75256_display_control(bool invert, st75256_color_mode_e mode);
void mk_st75256_text(char *s, uint8_t x, uint8_t y, uint16_t fg565, uint16_t bg565);

/* ── Inline helpers ──────────────────────────────────────────────────── */
/* Pack 4 gray values (top→bottom) into one vertical byte */
static inline uint8_t mk_st75256_pack_v(uint8_t r0, uint8_t r1,
                                         uint8_t r2, uint8_t r3)
{
    return (uint8_t)(((r0 & 3u) << 6) | ((r1 & 3u) << 4) |
                     ((r2 & 3u) << 2) |  (r3 & 3u));
}

/* RGB565 → 4-gray (0=white … 3=black) for text/UI colors */
static inline uint8_t mk_st75256_rgb565_to_gray(uint16_t rgb)
{
    uint16_t r = (rgb >> 11) & 0x1Fu;
    uint16_t g = (rgb >>  5) & 0x3Fu;
    uint16_t b =  rgb        & 0x1Fu;
    uint32_t luma = (uint32_t)(r * 4u + g * 5u + b * 2u);
    uint8_t  level = (uint8_t)((luma * 4u) / 502u);
    if (level > 3u) level = 3u;
    return (uint8_t)(3u - level);
}
