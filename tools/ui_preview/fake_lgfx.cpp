// Software renderer for the preview harness, using the real LovyanGFX font tables.
#include "LovyanGFX.hpp"
#include <Arduino.h>

namespace fonts {
using lgfx::GFXfont;
using lgfx::GFXglyph;
#include "lgfx/Fonts/GFXFF/FreeSans9pt7b.h"
#include "lgfx/Fonts/GFXFF/FreeSans12pt7b.h"
#include "lgfx/Fonts/GFXFF/FreeSansBold9pt7b.h"
#include "lgfx/Fonts/GFXFF/FreeSansBold12pt7b.h"
#include "lgfx/Fonts/GFXFF/FreeSansBold18pt7b.h"
#include "lgfx/Fonts/GFXFF/FreeSansBold24pt7b.h"
namespace f16 {
#include "lgfx/Fonts/Font16.h"
}
const lgfx::BMPfont Font2{f16::chrtbl_f16, f16::widtbl_f16, 16, 13};
}  // namespace fonts

namespace lgfx {

static LGFX_Device* g_target = nullptr;

int GFXfont::drawChar(uint16_t*, int x, int y_top, char c, uint16_t fg) const {
    if (c < first || c > last) return 0;
    const GFXglyph& g = glyph[c - first];
    const int base = y_top + ab();
    uint32_t bit = 0;
    const uint8_t* bm = bitmap + g.bitmapOffset;
    for (int r = 0; r < g.height; ++r)
        for (int col = 0; col < g.width; ++col, ++bit)
            if (bm[bit >> 3] & (0x80 >> (bit & 7))) g_target->pixel(x + g.xOffset + col, base + g.yOffset + r, fg);
    return g.xAdvance;
}

int BMPfont::drawChar(uint16_t*, int x, int y_top, char c, uint16_t fg) const {
    if (c < 32 || c >= 128) return 0;
    const int w = widtbl[c - 32];
    const unsigned char* d = chrtbl[c - 32];
    const int bpr = (w + 6) / 8;   // bytes per row, as TFT_eSPI / LovyanGFX
    for (int r = 0; r < h; ++r)
        for (int col = 0; col < w; ++col)
            if (d[r * bpr + (col >> 3)] & (0x80 >> (col & 7))) g_target->pixel(x + col, y_top + r, fg);
    return w;
}

void LGFX_Device::fillRect(int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) pixel(i, j, c);
}

void LGFX_Device::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
            const int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
            if (dx * dx + dy * dy <= r * r) pixel(x + i, y + j, c);
        }
}

void LGFX_Device::drawCircle(int x0, int y0, int r, uint16_t c) {
    for (int a = 0; a < 360; ++a) {
        const double t = a * 3.14159265 / 180.0;
        pixel(x0 + static_cast<int>(r * __builtin_cos(t) + 0.5), y0 + static_cast<int>(r * __builtin_sin(t) + 0.5), c);
    }
}

void LGFX_Device::drawString(const char* s, int x, int y) {
    if (!font) return;
    g_target = this;
    const int w = font->width(s), h = font->height();
    if (datum == middle_center || datum == top_center) x -= w / 2;
    if (datum == middle_center || datum == middle_left) y -= h / 2;
    if (has_bg) fillRect(x, y, w, h, bg);
    for (; *s; ++s) x += font->drawChar(fb, x, y, *s, fg);
}

}  // namespace lgfx
