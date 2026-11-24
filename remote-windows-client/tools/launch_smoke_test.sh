#!/bin/bash
# 5-second launch smoke test for the macOS Client executable.
BIN="$(cd "$(dirname "$0")/.." && swift build --show-bin-path)/RemoteWindowsApp"
"$BIN" &
APP_PID=$!
sleep 4
if kill -0 "$APP_PID" 2>/dev/null; then
  echo "APP RUNNING after 4s (pid $APP_PID)"
  kill "$APP_PID"
  sleep 1
  kill -0 "$APP_PID" 2>/dev/null && echo "still alive, force kill" && kill -9 "$APP_PID"
else
  echo "APP EXITED EARLY (crash?)"
fi
wait "$APP_PID" 2>/dev/null
echo "smoke-test done"
