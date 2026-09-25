/* test_font.c - glyph table, width measurement and clipped drawing. */
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "test.h"

static canvas make_canvas(int w, int h) {
    canvas c;
    c.w = w;
    c.h = h;
    c.px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    return c;
}

static int count_lit(const canvas *c, uint32_t color) {
    int n = 0;
    for (int i = 0; i < c->w * c->h; i++)
        n += c->px[i] == color;
    return n;
}

static int glyph_bits(char ch) {
    const uint8_t *g = font_glyph(ch);
    int n = 0;
    for (int r = 0; r < 8; r++)
        for (int b = 0; b < 8; b++)
            n += (g[r] >> b) & 1;
    return n;
}

static void dump_glyphs(void) {
    for (int ch = 32; ch <= 126; ch++) {
        const uint8_t *g = font_glyph((char)ch);
        printf("'%c'\n", ch);
        for (int r = 0; r < 8; r++) {
            for (int b = 0; b < 8; b++)
                putchar((g[r] >> b) & 1 ? '#' : '.');
            putchar('\n');
        }
    }
}

static void test_glyph_a(void) {
    static const uint8_t a[8] = {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00};
    CHECK(memcmp(font_glyph('A'), a, 8) == 0);
}

static void test_space_empty(void) {
    CHECK(glyph_bits(' ') == 0);
    for (int ch = 33; ch <= 126; ch++)
        CHECK(glyph_bits((char)ch) > 0);
}

static void test_unknown_is_question(void) {
    const uint8_t *q = font_glyph('?');
    CHECK(font_glyph('\t') == q);
    CHECK(font_glyph((char)127) == q);
    CHECK(font_glyph((char)200) == q);
    CHECK(font_glyph('\0') == q);
}

static void test_width(void) {
    CHECK(font_width("", 1) == 0);
    CHECK(font_width("abc", 1) == 24);
    CHECK(font_width("abc", 2) == 48);
    CHECK(font_width("ab\nabcd\nx", 1) == 32);
    CHECK(font_width("abcd\n", 3) == 96);
    CHECK(font_width("\n\n", 1) == 0);
}

static void test_draw_exact_pixels(void) {
    canvas c = make_canvas(40, 30);
    uint32_t col = 0xFFFFFF;
    const uint8_t *g = font_glyph('A');
    int w = font_draw(&c, 3, 5, "A", col, 1);
    CHECK(w == 8);
    int ok = 1;
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++) {
            int gx = x - 3, gy = y - 5;
            int lit = gx >= 0 && gx < 8 && gy >= 0 && gy < 8 && ((g[gy] >> gx) & 1);
            if ((c.px[y * c.w + x] == col) != lit)
                ok = 0;
        }
    CHECK(ok);
    free(c.px);
}

static void test_newline_offset(void) {
    canvas c = make_canvas(40, 40);
    int w = font_draw(&c, 0, 0, "_\n_", 0x00FF00, 2);
    CHECK(w == 16);
    /* '_' lights row 7 only: at scale 2, rows 14-15 and (18 lower) 32-33. */
    CHECK(c.px[14 * 40 + 0] == 0x00FF00);
    CHECK(c.px[15 * 40 + 15] == 0x00FF00);
    CHECK(c.px[32 * 40 + 0] == 0x00FF00);
    CHECK(c.px[33 * 40 + 15] == 0x00FF00);
    CHECK(c.px[16 * 40 + 0] == 0);
    CHECK(count_lit(&c, 0x00FF00) == 2 * 16 * 2);
    free(c.px);
}

static void test_clip(void) {
    canvas c = make_canvas(10, 10);
    font_draw(&c, -5, -5, "Hello, world!\nMMMM", 0xFF0000, 3);
    font_draw(&c, 7, 7, "WWW", 0xFF0000, 2);
    font_draw(&c, -1000, 5000, "x", 0xFF0000, 1);
    CHECK(count_lit(&c, 0xFF0000) > 0);
    CHECK(font_draw(NULL, 0, 0, "ab", 0xFF0000, 1) == 16);
    free(c.px);
}

static void test_scale_area(void) {
    const char *s = "starsim 42 %";
    canvas c1 = make_canvas(200, 40), c2 = make_canvas(200, 40);
    font_draw(&c1, 0, 0, s, 0xABCDEF, 1);
    font_draw(&c2, 0, 0, s, 0xABCDEF, 2);
    int n1 = count_lit(&c1, 0xABCDEF), n2 = count_lit(&c2, 0xABCDEF);
    int bits = 0;
    for (const char *p = s; *p; p++)
        bits += glyph_bits(*p);
    CHECK(n1 == bits);
    CHECK(n2 == 4 * n1);
    free(c1.px);
    free(c2.px);
}

static void test_alpha_background_kept(void) {
    canvas c = make_canvas(8, 8);
    for (int i = 0; i < 64; i++)
        c.px[i] = 0x102030;
    font_draw(&c, 0, 0, ".", 0xFFFFFF, 1);
    CHECK(count_lit(&c, 0xFFFFFF) == glyph_bits('.'));
    CHECK(count_lit(&c, 0x102030) == 64 - glyph_bits('.'));
    free(c.px);
}

int main(void) {
        dump_glyphs();
    RUN(test_glyph_a);
    RUN(test_space_empty);
    RUN(test_unknown_is_question);
    RUN(test_width);
    RUN(test_draw_exact_pixels);
    RUN(test_newline_offset);
    RUN(test_clip);
    RUN(test_scale_area);
    RUN(test_alpha_background_kept);
    return TEST_SUMMARY();
}
