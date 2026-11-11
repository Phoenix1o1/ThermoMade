#!/usr/bin/env bash
# One command to check everything; prints one line per step and the log tail only on failure.
#   tools/check.sh          all steps (tests, 3 firmware builds, golden vectors, screens, web page, log tool)
#   tools/check.sh --quick  no firmware builds / screen render (seconds instead of minutes)
set -uo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
quick=0; [ "${1:-}" = "--quick" ] && quick=1
tmp="$(mktemp -d)"; fail=0
step() {  # step "name" command...
  local name="$1"; shift
  if "$@" >"$tmp/log" 2>&1; then echo "PASS  $name"; else echo "FAIL  $name"; tail -15 "$tmp/log" | sed 's/^/      /'; fail=1; fi
}
cd "$repo/firmware"
step "unit tests (native)" pio test -e native
if [ $quick = 0 ]; then
  for e in base_cyd node_sht4x node_sht31 node_dht22; do step "build $e" pio run -e "$e"; done
  step "no warnings in our code (base)" bash -c "! (touch src/base/main.cpp; pio run -e base_cyd 2>&1 | grep -E '(src|lib/tm_)[^ ]*: warning')"
fi
cd "$repo"
step "golden vectors match the simulator" bash -c "cd tools/sim && python3 gen_golden.py $tmp/golden.h >/dev/null && diff -q $tmp/golden.h ../../firmware/test/test_golden/golden_data.h"
if [ $quick = 0 ]; then step "screens render (ui_preview)" tools/ui_preview/build.sh "$tmp/ui"; fi
if python3 -c "import playwright" 2>/dev/null; then step "web page in a browser (mock API)" python3 tools/web_mock/test_page.py
else echo "SKIP  web page test (pip install playwright && playwright install chromium)"; fi
step "log analysis tool (fake logs)" bash -c "python3 tools/analysis/make_fake_logs.py -o $tmp/logs --days 2 && python3 tools/analysis/tm_logs.py summary $tmp/logs"
rm -rf "$tmp"
[ $fail = 0 ] && echo "ALL OK" || echo "SOMETHING FAILED"
exit $fail
