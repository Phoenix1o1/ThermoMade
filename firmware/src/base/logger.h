// TermoMade base — microSD logging (VSPI 18/19/23/5), in its own low-priority task on core 0.
//  * /log/YYYY-MM-DD.csv : one line per minute (temperatures, setpoint, u, valve, counters)
//  * /log/events.csv     : valve open/close, mode and setpoint changes, alarms, boots, ...
// Lines are queued without waiting; the control task never blocks on the card. Each line is
// appended and the file closed again, so a power cut loses at most the line being written.
// A missing or failing card only disables logging (retried every minute).
#pragma once
#include <cstdint>
#include "shared.h"

namespace logger {
void begin();                                        // mount the card and start the task
void sample(const Snapshot& s);                      // control task, every kLogEveryMs
void event(const tmc::Event& e, uint32_t unix_utc);  // control task
void text(const char* line, uint32_t unix_utc);      // free text into events.csv (boot info, time set)
bool sdOk();
uint32_t dropped();                                  // lines lost because the queue was full
}  // namespace logger
