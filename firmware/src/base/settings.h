// TermoMade base — persistent settings in NVS.
//  * "cfg" blob: parameters, schedule, mode, touch calibration, display, radio (CRC + version)
//  * "ps"  blob: learned state (I per band, CYD sensor offset), saved every 30 min
// A blob with a wrong magic/version/size/CRC is ignored -> defaults (never half-applied).
#pragma once
#include <cstdint>
#include "tm_controller.h"

struct TouchCal {
    int16_t x_min = 300, x_max = 3800;   // raw X at screen left / right
    int16_t y_min = 300, y_max = 3800;   // raw Y at screen top / bottom
    uint8_t swap_xy = 1;                 // landscape on the CYD: raw Y -> screen X
    uint8_t invert_x = 0;
    uint8_t invert_y = 0;
};

struct Settings {
    tmc::ControlParams params;
    tmc::WeekSchedule schedule = tmc::WeekSchedule::makeDefault();
    tmc::Mode mode = tmc::Mode::Auto;
    TouchCal touch;
    uint8_t screen_timeout_s = 30;
    uint8_t brightness = 180;            // 0..255 when the screen is on
    uint8_t net_id = 0;                  // ESP-NOW network id, generated at first boot
    uint8_t ap_channel = 1;
};

namespace settings {
// Load (defaults if missing/corrupt). Generates a net_id on first boot.
Settings load();
bool save(const Settings& s);
bool loadPersistent(tmc::PersistentState* ps);
bool savePersistent(const tmc::PersistentState& ps);
void factoryReset();
}  // namespace settings
