# Changelog

## v2.0 — Hardened (2026-10)

Main release: [`firmware/ortam_aurora/ortam_aurora.ino`](firmware/ortam_aurora/ortam_aurora.ino). Same features as v1; every way into the device now needs a per-device secret.

**Status:** independent review passed (2 rounds; two minor notes applied). Flashed and running on the device (owner-reported, 2026-10-06).

- **Wi-Fi setup:** the setup network `OrtamMonitor-Setup` is now WPA2-protected with a random 12-character password made on first boot — strangers can no longer join it.
- **Web API:** every `/api/*` call needs `Authorization: Bearer <token>` — someone else on your Wi-Fi can no longer read or control the device.
- **No drive-by clicks:** LED and alarm changes are POST-only — a malicious web page can no longer switch your lights by loading a link.
- **WebSocket:** a client must send the token within 5 seconds or it is disconnected — live data only goes to clients that proved who they are, and idle sockets cannot use up the slots.
- **Bluetooth:** pairing with a 6-digit PIN is required for every read and write, with a fresh random PIN shown on the OLED for each pairing — people nearby can no longer read your room data or control the light.
- **Bluetooth clean-up:** phones that do not finish pairing are dropped within about 30 seconds, and no live notifications are sent while such a phone is connected (checked against the live connection list before every send).
- **Bluetooth after setup:** the device becomes visible over Bluetooth only once start-up is finished, so the pairing PIN can always be shown on the OLED.
- **Fail closed:** if the secrets cannot be saved to flash, the API and setup network stay locked instead of falling back to open.
- **Secrets on first boot:** the AP password and API token are printed to Serial and shown on the OLED (15 s) once, on first boot. The API token is not shown again; the AP password is shown on the OLED every time the setup portal opens.
- **Location is config:** latitude, longitude and UTC offset live in one `LOCATION` block instead of being hard-coded. v2 default: city-level, İzmir city centre (38.4237, 27.1428, UTC+3) — set your own city.

## v1.0 — Field build

Archived as-is: [`firmware/legacy/v1_field/esp32_monitor_v7.ino`](firmware/legacy/v1_field/esp32_monitor_v7.ino) (file header says "v8.0 - Sunum Sürümü" (presentation build)).

**Status:** field-proven on the owner's device before v2 replaced it.

- Room sensing: temperature/humidity (DHT22), light (BH1750), eCO2/TVOC (SGP30, baseline saved in flash), sound level (MAX4466).
- Sunrise alarm: fetches today's sunrise time and fades a 60-LED WS2812B strip from red to warm white over 30 minutes; skipped if the room is already bright.
- Live views: OLED, web dashboard with WebSocket updates, REST API, BLE GATT service for a phone app.
- Robustness: WiFiManager captive setup portal, Wi-Fi auto-reconnect, BLE keeps working without Wi-Fi, 30 s watchdog.
- Security: none — open setup network, open API (state changes via GET), open WebSocket, open BLE. See [SECURITY.md](SECURITY.md).
- Public-copy change: the field build's city-level coordinates (legacy: Istanbul city centre; the v2 default is İzmir city centre) were replaced with a neutral `0.0, 0.0` placeholder in the legacy `KONUM` block. Otherwise the file is identical to the owner's last saved source file (the timezone stays hard-coded to UTC+3); that it matches the binary flashed on the device cannot be proven.
