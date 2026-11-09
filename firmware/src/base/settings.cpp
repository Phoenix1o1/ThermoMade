#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <rom/crc.h>

namespace {
constexpr uint32_t kMagic = 0x544D5331;   // "TMS1"
constexpr uint16_t kVersion = 1;          // bump on any layout change of Settings

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
    if (!loadBlob("cfg", &s)) {
        Serial.println("[settings] no valid settings in NVS -> defaults");
        s = Settings{};
    }
    tmc::sanitize(s.params);
    s.schedule.sanitize();
    if (static_cast<uint8_t>(s.mode) > 4) s.mode = tmc::Mode::Auto;
    if (s.screen_timeout_s < 10) s.screen_timeout_s = 30;
    if (s.ap_channel < 1 || s.ap_channel > 13) s.ap_channel = 1;
    if (s.net_id == 0) {
        s.net_id = static_cast<uint8_t>(1 + esp_random() % 254);
        save(s);
        Serial.printf("[settings] new network id %u\n", s.net_id);
    }
    return s;
}

bool save(const Settings& s) { return saveBlob("cfg", s); }

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
