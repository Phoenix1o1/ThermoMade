#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <rom/crc.h>

namespace {
constexpr uint32_t kMagic = 0x544D5331;   // "TMS1"
constexpr uint16_t kVersion = 4;          // bump on any layout change of Settings (2: arrival, auto-brightness)

template <typename T>
struct Blob {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    T data;
    uint32_t crc;
};

template <typename T>
uint32_t crcOf(const Blob<T>& b) {
    return crc32_le(0, reinterpret_cast<const uint8_t*>(&b), offsetof(Blob<T>, crc));
}

template <typename T>
bool loadBlob(const char* key, T* out) {
    Preferences pr;
    if (!pr.begin("termomade", true)) return false;
    Blob<T> b;
    const size_t n = pr.getBytes(key, &b, sizeof b);
    pr.end();
    if (n != sizeof b || b.magic != kMagic || b.version != kVersion || b.size != sizeof(T) || b.crc != crcOf(b))
        return false;
    *out = b.data;
    return true;
}

template <typename T>
bool saveBlob(const char* key, const T& v) {
    Blob<T> b;
    memset(&b, 0, sizeof b);
    b.magic = kMagic;
    b.version = kVersion;
    b.size = sizeof(T);
    b.data = v;
    b.crc = crcOf(b);
    Preferences pr;
    if (!pr.begin("termomade", false)) return false;
    const size_t n = pr.putBytes(key, &b, sizeof b);
    pr.end();
    return n == sizeof b;
}
}  // namespace

namespace settings {

Settings load() {
    Settings s;
    const bool cfg_ok = loadBlob("cfg", &s);
    if (!cfg_ok) {
        Serial.println("[settings] no valid settings in NVS -> defaults");
        s = Settings{};
    }
    tmc::sanitize(s.params);
    s.schedule.sanitize();
    // net_id and mode also live under their own keys: a settings layout change or a damaged blob
    // must not orphan the paired nodes (new random net_id) or turn an AWAY house back to AUTO.
    {
        Preferences pr;
        if (pr.begin("termomade", true)) {
            const uint8_t nid = pr.getUChar("netid", 0), md = pr.getUChar("mode", 0xFF);
            pr.end();
            if (!cfg_ok && nid != 0) s.net_id = nid;
            if (!cfg_ok && md <= 4) s.mode = static_cast<tmc::Mode>(md);
        }
    }
    if (static_cast<uint8_t>(s.mode) > 4) s.mode = tmc::Mode::Auto;
    if (s.screen_timeout_s < 10) s.screen_timeout_s = 30;
    if (s.brightness < 10) s.brightness = 180;
    if (s.brightness_min < 1 || s.brightness_min > s.brightness) s.brightness_min = 12;
    s.auto_brightness = s.auto_brightness ? 1 : 0;
    if (s.ap_channel < 1 || s.ap_channel > 13) s.ap_channel = 1;
    if (s.net_id == 0) {
        s.net_id = static_cast<uint8_t>(1 + esp_random() % 254);
        save(s);
        Serial.printf("[settings] new network id %u\n", s.net_id);
    }
    return s;
}

bool save(const Settings& s) {
    Preferences pr;
    if (pr.begin("termomade", false)) {
        if (pr.getUChar("netid", 0) != s.net_id) pr.putUChar("netid", s.net_id);
        if (pr.getUChar("mode", 0xFF) != static_cast<uint8_t>(s.mode)) pr.putUChar("mode", static_cast<uint8_t>(s.mode));
        pr.end();
    }
    return saveBlob("cfg", s);
}

bool loadPersistent(tmc::PersistentState* ps) { return loadBlob("ps", ps); }

bool savePersistent(const tmc::PersistentState& ps) { return saveBlob("ps", ps); }

void factoryReset() {
    Preferences pr;
    pr.begin("termomade", false);
    pr.clear();
    pr.end();
    pr.begin("tmnodes", false);
    pr.clear();
    pr.end();
}

}  // namespace settings
