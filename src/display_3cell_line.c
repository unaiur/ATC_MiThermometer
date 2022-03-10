#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "display_3cell_line.h"
#include "display_drv.h"

_attribute_ram_code_ void display_render_3cell_number(const struct display_3cell_line *line, int16_t value)
{
    int max_value = (line->render_leftmost_one ? 1999 : 999);
    bool show_decimal_dot = (value >= -99 && value <= max_value);
    if (!show_decimal_dot) {
        // Divide by ten and round: add 5 to absolute value and divide by ten (which always rounds towards zero)
        value += (value < 0 ? -5 : 5);
        value /= 10;
    }
    line->render_decimal_dot(show_decimal_dot);
    if (line->render_leftmost_one) {
        line->render_leftmost_one(value >= 1000 && value <= 1999);
    }
    if (value < -99) {
        line->render_cell(0, CHR_L);
        line->render_cell(1, CHR_o);
        line->render_cell(2, CHR_SPACE);
    } else if (value > max_value) {
        line->render_cell(0, CHR_H);
        line->render_cell(1, CHR_i);
        line->render_cell(2, CHR_SPACE);
    } else if (value < 0) {
        value = -value;
        line->render_cell(0, CHR_MINUS);
        line->render_cell(1, CHR_0 + value / 10);
        line->render_cell(2, CHR_0 + value % 10);
    } else {
        line->render_cell(2, CHR_0 + value % 10);
        value /= 10;
        line->render_cell(1, CHR_0 + value % 10);
        value /= 10;
        line->render_cell(0, value ? CHR_0 + value % 10 : CHR_SPACE);
    }
}


