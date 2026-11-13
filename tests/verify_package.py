#!/usr/bin/env python3
"""Validate package payload without installing or restarting CoreAudio."""
import hashlib
from pathlib import Path
import plistlib
import subprocess
import tempfile
import xml.etree.ElementTree as ET

repo = Path(__file__).resolve().parents[1]
version = (repo / 'VERSION').read_text().strip()
pkg = repo / 'dist' / f'Audio2DJ-Experimental-{version}-intel.pkg'
with tempfile.TemporaryDirectory(prefix='audio2dj-package-check-') as folder:
    expanded = Path(folder) / 'expanded'
    subprocess.run(['pkgutil', '--expand-full', str(pkg), str(expanded)], check=True)
    payload = expanded / 'Payload'
    app = payload / 'Library/Application Support/Audio2DJExperimental'
    hal = payload / 'Library/Audio/Plug-Ins/HAL/Audio2DJ.driver'
    def digest(path):
        return hashlib.sha256(path.read_bytes()).digest()
    assert digest(app / 'audio2dj-bridge') == digest(repo / 'dist/audio2dj-bridge')
    assert digest(hal / 'Contents/MacOS/Audio2DJ') == digest(repo / 'dist/Audio2DJ.driver/Contents/MacOS/Audio2DJ')
    assert 'x86_64' in subprocess.check_output(['file', str(app / 'audio2dj-bridge')], text=True)
    subprocess.run(['codesign', '--verify', '--strict', str(hal)], check=True)
    agent = plistlib.loads((payload / 'Library/LaunchAgents/org.local.audio2dj.bridge.plist').read_bytes())
    assert agent['Label'] == 'org.local.audio2dj.bridge'
    assert agent['ProgramArguments'] == ['/bin/sh', '/Library/Application Support/Audio2DJExperimental/launch-bridge.sh']
    info = ET.parse(expanded / 'PackageInfo').getroot()
    assert info.attrib['identifier'] == 'org.local.audio2dj.experimental.pkg'
    assert info.attrib['install-location'] == '/'
    for name in ('preinstall', 'postinstall'):
        subprocess.run(['bash', '-n', str(expanded / 'Scripts' / name)], check=True)
    for name in ('audio2dj-bridge', 'launch-bridge.sh', 'uninstall.sh'):
        assert (app / name).stat().st_mode & 0o111
    for name in ('LICENSE', 'LICENSE.apple2012', 'LICENSE.apple2020', 'COPYING.libusb'):
        assert (app / 'licenses' / name).stat().st_size > 500
    assert not list(payload.rglob('*.kext'))
    assert not list(payload.rglob('*.f32'))
print('PASS: payload hashes, architecture, HAL signature, launch agent, scripts, licenses and absence of kext/audio captures.')
