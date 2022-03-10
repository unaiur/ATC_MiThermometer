#include <stdint.h>
#include "tl_common.h"
#include "display_drv.h"
#include "display_13seg_cell.h"


// Segment bitmaps associated with each character
static const uint16_t char_segment_bitmap[NUM_CHARS] = {
    [CHR_0] = BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_2R) | BIT(SEG_3L) | BIT(SEG_3R) | BIT(SEG_4L) | BIT(SEG_4R)
        | BIT(SEG_5M),
    [CHR_1] = BIT(SEG_1R) | BIT(SEG_2R) | BIT(SEG_3R) | BIT(SEG_4R) | BIT(SEG_5R),
    [CHR_2] = BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_3M) | BIT(SEG_4R) | BIT(SEG_5M),
    [CHR_3] = BIT(SEG_1M) | BIT(SEG_2R) | BIT(SEG_3M) | BIT(SEG_4R) | BIT(SEG_5M),
    [CHR_4] = BIT(SEG_1R) | BIT(SEG_2R) | BIT(SEG_3M) | BIT(SEG_3R) | BIT(SEG_4L) | BIT(SEG_4R) | BIT(SEG_5L),
    [CHR_5] = BIT(SEG_1M) | BIT(SEG_2R) | BIT(SEG_3L) | BIT(SEG_3M) | BIT(SEG_4L) | BIT(SEG_5L) | BIT(SEG_5M),
    [CHR_6] = BIT(SEG_1M) | BIT(SEG_2R) | BIT(SEG_2L) | BIT(SEG_3L) | BIT(SEG_3M) | BIT(SEG_4L) | BIT(SEG_5M),
    [CHR_7] = BIT(SEG_1R) | BIT(SEG_2R) | BIT(SEG_3R) | BIT(SEG_4R) | BIT(SEG_5M) | BIT(SEG_5R),
    [CHR_8] = BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_2R) | BIT(SEG_3M) | BIT(SEG_4L) | BIT(SEG_4R) | BIT(SEG_5M),
    [CHR_9] = BIT(SEG_1R) | BIT(SEG_2R) | BIT(SEG_3M) | BIT(SEG_3R) | BIT(SEG_4L) | BIT(SEG_4R) | BIT(SEG_5M),
    [CHR_L] = BIT(SEG_1R) | BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_3L) | BIT(SEG_4L),
    [CHR_o] = BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_2R) | BIT(SEG_3M),
    [CHR_H] = BIT(SEG_1L) | BIT(SEG_1R) | BIT(SEG_2L) | BIT(SEG_2R) | BIT(SEG_3L) | BIT(SEG_3M) | BIT(SEG_3R)
            | BIT(SEG_4L) | BIT(SEG_4R) | BIT(SEG_5L) | BIT(SEG_5R),
    [CHR_i] = BIT(SEG_1L) | BIT(SEG_2L) | BIT(SEG_5L),
    [CHR_MINUS] = BIT(SEG_3M),
    [CHR_SPACE] = 0,
};

_attribute_ram_code_ void display_render_13seg_char(const struct display_13seg_cell *cell, enum display_char c)
{
    if (c < NUM_CHARS) {
        uint16_t bitmap = char_segment_bitmap[c];
	const uint8_t *segment_bits = cell->segment_bits;
        for (int segment = sizeof cell->segment_bits; segment; --segment) {
            display_render_bit(*segment_bits++, bitmap & 1);
            bitmap >>= 1;
        }
    }
}

