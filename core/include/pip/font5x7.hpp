#pragma once
#include <cstdint>
namespace pip {
// The classic 5x7 ASCII bitmap (glcdfont / HD44780 shapes), ASCII 0x20..0x7E.
// Column-major: entry i is glyph 0x20+i, five columns left to right, bit 0 = top row,
// bit 6 = bottom row. 475 bytes of rodata.
extern const uint8_t FONT5X7[95][5];
// Anything outside the printable range prints as '?', so a stray byte from the wire shows
// up on the screen instead of indexing past the table.
inline const uint8_t* glyph(char c) {
    unsigned i = (unsigned char)c;
    return (i < 0x20 || i > 0x7E) ? FONT5X7['?' - 0x20] : FONT5X7[i - 0x20];
}
}
