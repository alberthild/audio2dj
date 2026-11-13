# Kurzanleitung

Experimenteller Treiber für das **originale Audio 2 DJ (17cc:041c)** auf Intel-Macs.
Geprüft auf einem MacBook Pro 2020 mit macOS 26.7.1 und djay Pro 5.6.9.

Das Intel-Paket unter GitHub Releases herunterladen. Vor Installation Audio-Apps
schließen und die Lautstärke herunterdrehen: CoreAudio wird neu gestartet.
Danach „Audio 2 DJ Experimental“ auswählen, 48 kHz, Master 1–2/A, Vorhören 3–4/B.
Keine SIP-Änderung und kein alter NI-Kernel-Treiber erforderlich.

Das Paket ist nicht Developer-ID-signiert/notarisiert. Die Installation auf einem
frischen Mac wurde noch nicht vollständig getestet; der laufende Prototyp wurde
manuell installiert. Hauptmix und Vorhören wurden sauber bestätigt. Spätere Logs
zeigen dennoch zwei USB-Abbrüche mit automatischem Wiederanlauf. Die USB-Warteschlange
umfasst etwa 128 ms und erzeugt deutlich spürbare Verzögerung. Keine Freigabe als
zuverlässiger Live-Treiber. Ein vollständiger Probelauf ist erforderlich.

Build, Deinstallation, Lizenzen und alle bekannten Einschränkungen: siehe README.md.
