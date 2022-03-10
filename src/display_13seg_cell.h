#pragma once
/** Handles drawing of chars in 13 segment cells.
 * Segments are named following SEG_<ROW><COL> format, where:
 *   ROW is a number from 1 to 5; 1 is the bottom, 3 the middle, 5 the top
 *   COL is either L(eft), M(iddle) or R(ight)
 */
#include "display_char.h"
#include "tl_common.h"
#include "drivers.h"

enum display_13seg_cell_segment {
    SEG_1L,
    SEG_2L,
    SEG_3L,
    SEG_4L,
    SEG_5L,
    SEG_1M,
    SEG_3M,
    SEG_5M,
    SEG_1R,
    SEG_2R,
    SEG_3R,
    SEG_4R,
    SEG_5R,
};

// Describes where are located the segments of a cell in the display buffer
struct display_13seg_cell
{
	uint8_t segment_bits[13];
};

// Draws a character in a given 13-segment cell
_attribute_ram_code_ void display_render_13seg_char(const struct display_13seg_cell *cell, enum display_char c);
