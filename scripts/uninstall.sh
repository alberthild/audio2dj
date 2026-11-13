#!/bin/bash
set -euo pipefail
if [[ ${1:-} != --yes ]]; then
 echo 'Stops Audio 2 DJ, removes its HAL plugin and package service, and restarts CoreAudio.'
 echo 'Backups and user logs are retained. Run: sudo bash uninstall.sh --yes'
 exit 0
fi
[[ $EUID == 0 ]] || { echo 'Run with sudo.' >&2; exit 1; }
uid=$(stat -f %u /dev/console)
if [[ "$uid" -ge 500 ]]; then /bin/launchctl bootout "gui/$uid/org.local.audio2dj.bridge" 2>/dev/null || true; fi
rm -f /Library/LaunchAgents/org.local.audio2dj.bridge.plist
rm -rf /Library/Audio/Plug-Ins/HAL/Audio2DJ.driver
rm -rf '/Library/Application Support/Audio2DJExperimental'
/usr/sbin/pkgutil --forget org.local.audio2dj.experimental.pkg >/dev/null 2>&1 || true
/usr/bin/killall coreaudiod 2>/dev/null || true
echo 'Package removed. Disabled legacy agents and HAL backups were not re-enabled.'
