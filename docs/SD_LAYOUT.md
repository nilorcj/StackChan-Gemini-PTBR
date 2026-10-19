# SD card layout

The firmware stores mutable runtime state on the SD card under `/app/StackChan/`.

## Directory tree

```text
/app/StackChan/
  config/
    runtime.json
    gateway.json
    summary.json
  prompts/
    system.txt
    persona.txt
  secrets/
    gemini_api_key.txt
    wifi_password.txt
    gateway_hmac_key.txt
    gateway_token.txt          # legacy (not used by POST /v2/call)
    calendar_url.txt           # legacy (not required for ToolGateway agenda)
    web_password_sha256.txt
    meta.jsonl
  memory/
    events/YYYY-MM-DD.jsonl
    dialogues/YYYY-MM-DD.jsonl
    summaries.jsonl
    memories.jsonl
    profile.json
  camera/
    latest.jpg
```

The firmware creates some directories automatically after SD mount. For first configuration, either prepare the files manually or use the setup access point Web UI after flashing.

## Minimal Wi-Fi + Web UI setup

Create:

```text
/app/StackChan/config/runtime.json
/app/StackChan/secrets/wifi_password.txt
```

Example `runtime.json`:

```json
{
  "robot_id": "stackchan",
  "wifi_enabled": true,
  "wifi_ssid": "YOUR_WIFI_SSID",
  "web_enabled": true,
  "gemini_enabled": false,
  "gateway_enabled": false
}
```

Example `wifi_password.txt`:

```text
YOUR_WIFI_PASSWORD
```

Boot the robot. If station Wi-Fi connects, read the IP from serial logs and open `http://ROBOT_IP/`. If credentials are missing or connection fails, join the open `<robot_id>-setup` access point and open `http://192.168.4.1/`. The Web UI can save Wi-Fi SSID/password and other settings; reboot after saving network changes.

## Gemini setup

Add:

```text
/app/StackChan/secrets/gemini_api_key.txt
```

and enable Gemini in `runtime.json`:

```json
{
  "gemini_enabled": true,
  "gemini_model": "models/gemini-3.1-flash-live-preview",
  "gemini_voice": "Puck"
}
```

You can also edit prompts:

```text
/app/StackChan/prompts/system.txt
/app/StackChan/prompts/persona.txt
```

## Optional gateway setup

Gateway integration (`web_search` and Google Calendar agenda via external ToolGateway/Cloudflare Worker) is optional. Add `/app/StackChan/config/gateway.json` (see `examples/gateway.example.json`) or set the equivalent fields in `runtime.json`:

```json
{
  "gateway_enabled": true,
  "gateway_base_url": "http://YOUR_GATEWAY_HOST",
  "robot_id": "stackchan"
}
```

### ToolGateway authentication (`POST /v2/call` + HMAC-SHA256)

* The productive firmware path calls `POST /v2/call` on `gateway_base_url`.
* Productive authentication uses **HMAC-SHA256** (`X-Timestamp`, `X-Nonce`, `X-Signature`). Store the shared secret separately on the microSD card in:

```text
/app/StackChan/secrets/gateway_hmac_key.txt
```

* `gateway_hmac_key.txt` is only required when `gateway_enabled` is `true` and must contain a **64-character hexadecimal** key matching the remote Worker/gateway `HMAC_SHARED_KEY`.
* The ESP32 system clock must be synchronized (valid NTP/RTC time) so the firmware can generate a valid Unix timestamp for the HMAC signature.
* **Transport note:** HMAC authenticates the request and protects its integrity, and the timestamp/nonce help mitigate stale or repeated requests, but HMAC by itself does **not** encrypt HTTP traffic or completely eliminate replay within the server's accepted timestamp window.

### Legacy secret files

* `/app/StackChan/secrets/gateway_token.txt` is **legacy** and is **not** used by the productive `POST /v2/call` HMAC flow.
* `/app/StackChan/secrets/calendar_url.txt` is **legacy** from the previous local ICS implementation and is **not** required for the current ToolGateway agenda flow (the private Google Calendar ICS URL is configured on the remote Worker/server as `CALENDAR_ICS_URL`).

## Secrets policy

Never commit real files from `/app/StackChan/secrets/` or private runtime SD dumps. The Web/API status endpoints redact secret values as `set` or `missing`.
