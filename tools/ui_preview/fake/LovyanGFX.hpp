// UI preview harness: a tiny software LovyanGFX look-alike. Implements only the calls the TermoMade
// UI uses, renders into a 320x240 RGB565 framebuffer with the REAL font data from the LovyanGFX
// library (Adafruit GFX FreeSans fonts + Font2), so text widths and positions match the device.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define SPI2_HOST 1
#define SPI_DMA_CH_AUTO 3

namespace lgfx {
enum textdatum_t : uint8_t { top_left = 0, top_center = 1, middle_left = 4, middle_center = 5 };

struct IFont {
    virtual ~IFont() = default;
    virtual int width(const char* s) const = 0;
    virtual int height() const = 0;
    virtual int baseline() const = 0;
    // draws one char with its top-left text box corner at (x, y_top); returns advance
    virtual int drawChar(uint16_t* fb, int x, int y_top, char c, uint16_t fg) const = 0;
};

struct GFXglyph {
    uint32_t bitmapOffset;
    uint8_t width, height;
    uint8_t xAdvance;
    int8_t xOffset, yOffset;
};

struct GFXfont : IFont {
    const uint8_t* bitmap;
    const GFXglyph* glyph;
    uint16_t first, last;
    uint8_t yAdvance;
    GFXfont(uint8_t* b, GFXglyph* g, uint16_t f, uint16_t l, uint8_t ya) : bitmap(b), glyph(g), first(f), last(l), yAdvance(ya) {}
    int ab() const {
        int m = 0;
        for (int c = 0; c <= last - first; ++c) m = -glyph[c].yOffset > m ? -glyph[c].yOffset : m;
        return m;
    }
    int bb() const {
        int m = 0;
        for (int c = 0; c <= last - first; ++c) {
            const int b = glyph[c].height + glyph[c].yOffset;
            m = b > m ? b : m;
        }
        return m;
    }
    int width(const char* s) const override {
        int w = 0;
        for (; *s; ++s)
            if (*s >= first && *s <= last) w += glyph[*s - first].xAdvance;
        return w;
    }
    int height() const override { return ab() + bb(); }
    int baseline() const override { return ab(); }
    int drawChar(uint16_t* fb, int x, int y_top, char c, uint16_t fg) const override;
};

struct BMPfont : IFont {
    const unsigned char* const* chrtbl;
    const unsigned char* widtbl;
    int h, base;
    BMPfont(const unsigned char* const* c, const unsigned char* w, int hh, int b) : chrtbl(c), widtbl(w), h(hh), base(b) {}
    int width(const char* s) const override {
        int w = 0;
        for (; *s; ++s)
            if (*s >= 32 && *s < 128) w += widtbl[*s - 32];
        return w;
    }
    int height() const override { return h; }
    int baseline() const override { return base; }
    int drawChar(uint16_t* fb, int x, int y_top, char c, uint16_t fg) const override;
};

class LGFX_Device;
inline LGFX_Device* g_device = nullptr;   // the display the UI created (for screenshots)

class LGFX_Device {
public:
    static constexpr int W = 320, H = 240;
    uint16_t fb[W * H] = {};
    LGFX_Device() { g_device = this; }
    virtual ~LGFX_Device() = default;
    void init() {}
    void setRotation(int) {}
    void setBrightness(uint8_t b) { brightness = b; }
    void sleep() { asleep = true; }
    void wakeup() { asleep = false; }
    void fillScreen(uint16_t c) { fillRect(0, 0, W, H, c); }
    void fillRect(int x, int y, int w, int h, uint16_t c);
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
    void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
    void drawCircle(int x, int y, int r, uint16_t c);
    void setFont(const IFont* f) { font = f; }
    void setTextColor(uint16_t fg) { this->fg = fg; has_bg = false; }
    void setTextColor(uint16_t fg, uint16_t bg) { this->fg = fg; this->bg = bg; has_bg = true; }
    void setTextDatum(textdatum_t d) { datum = d; }
    int textWidth(const char* s) const { return font ? font->width(s) : 0; }
    void drawString(const char* s, int x, int y);
    void setPanel(void*) {}
    void pixel(int x, int y, uint16_t c) {
        if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = c;
    }
    uint8_t brightness = 0;
    bool asleep = false;

protected:
    const IFont* font = nullptr;
    uint16_t fg = TFT_WHITE, bg = TFT_BLACK;
    bool has_bg = false;
    textdatum_t datum = top_left;
};

// --- panel / bus / light configuration stand-ins (fields used by ui.cpp) ---
struct BusCfg {
    int spi_host, spi_mode, freq_write, freq_read;
    bool spi_3wire, use_lock;
    int dma_channel, pin_sclk, pin_mosi, pin_miso, pin_dc;
};
struct PanelCfg {
    int pin_cs, pin_rst, pin_busy, panel_width, panel_height, memory_width, memory_height, offset_x, offset_y,
        offset_rotation;
    bool readable, invert, rgb_order, dlen_16bit, bus_shared;
};
struct LightCfg {
    int pin_bl;
    bool invert;
    int freq, pwm_channel;
};
struct Bus_SPI {
    BusCfg c{};
    BusCfg config() const { return c; }
    void config(const BusCfg& n) { c = n; }
};
struct Light_PWM {
    LightCfg c{};
    LightCfg config() const { return c; }
    void config(const LightCfg& n) { c = n; }
};
struct Panel_Base {
    PanelCfg c{};
    PanelCfg config() const { return c; }
    void config(const PanelCfg& n) { c = n; }
    void setBus(Bus_SPI*) {}
    void setLight(Light_PWM*) {}
};
struct Panel_ST7789 : Panel_Base {};
struct Panel_ILI9341 : Panel_Base {};
}  // namespace lgfx

namespace fonts {
extern const lgfx::GFXfont FreeSans9pt7b, FreeSans12pt7b, FreeSansBold9pt7b, FreeSansBold12pt7b, FreeSansBold18pt7b,
    FreeSansBold24pt7b;
extern const lgfx::BMPfont Font2;
}  // namespace fonts
