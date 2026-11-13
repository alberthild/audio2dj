#!/bin/sh
# Runs as the logged-in user, never as a LaunchDaemon/root service.
set -eu
mkdir -p "$HOME/Library/Logs"
log="$HOME/Library/Logs/Audio2DJExperimental.log"
if [ -f "$log" ] && [ "$(stat -f %z "$log")" -gt 5242880 ]; then
 mv -f "$log" "$log.previous"
fi
exec '/Library/Application Support/Audio2DJExperimental/audio2dj-bridge' >>"$log" 2>&1
