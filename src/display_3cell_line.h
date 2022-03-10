#pragma once

/**
 * Displays a number in a line with 3 cells, with a decimal dot between the middle and right cells.
 * Optionally, there can be a 1 at the left, allowing to show numbers up to 199.9 or 1999
 */
#include "display_char.h"

struct display_3cell_line
{
	void (*render_cell)(int num_cell, enum display_char c);
	void (*render_decimal_dot)(bool on);
	void (*render_leftmost_one)(bool on); // optional
};

/** Draws a number expressed in tenths in the top or bottom rows
 *  @param where is the left cell of the chosen row
 *  @param number is the value to write
 *  @param dot_symbol is the dot symbol to use
 *  @param hundreds_symbol is the hundreds symbol to use (0 for the bottom row)
 *
 * If the number is out of bound (lower than -994 or larger than 19994 or 9994),
 * it shows Lo and Hi respectively.
 *
 * Examples:
 *  * 19995 shows  Hi
 *  * 19994 shows 199.9 (or Hi in the bottom row)
 *  *  9994 shows  99 9
 *  *   999 shows  99.9
 *  *   -99 shows  -9.9
 *  *  -994 shows  -9 9
 *  *  -995 shows  Lo
 */
void display_render_3cell_number(const struct display_3cell_line *line, int16_t value);
