// TermoMade base — serial console (115200 baud, line based). Type "help".
// Runs in loop(); it never touches the controller directly: every change is a Request.
#pragma once

namespace cli {
void loop();   // non-blocking: reads what is available, executes complete lines
}  // namespace cli
