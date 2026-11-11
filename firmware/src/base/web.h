// TermoMade base — web page + JSON API on the base's own Wi-Fi (http://192.168.4.1, no internet).
// One page: live status, setpoint, modes, ARRIVO/OFF, boost, clock sync from the phone, and an
// "Impostazioni" section to change every parameter, the schedule and the screen settings on the
// fly. Handlers run in the async TCP task and never touch the controller: they read the Snapshot
// and post Requests, exactly like the touch screen and the console (validated by the control task).
// API reference: docs/07-user-interface.md "Web page and JSON API".
#pragma once

namespace web {
void begin();   // after the Wi-Fi AP is up
}
