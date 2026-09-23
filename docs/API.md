# HTTP API overview

The Web server starts only when Wi-Fi connects and `web_enabled=true`.

If a Web password is configured, authenticate with HTTP Basic Auth:

```bash
curl -u stackchan:YOUR_PASSWORD http://ROBOT_IP/api/status
```

## Configuration and security

- `GET /api/security` — reports whether Web password is configured.
- `POST /api/security` — set/change Web password.
- `GET /api/status` — runtime status.
- `GET /api/runtime` — redacted runtime config.
- `POST /api/runtime` — save runtime fields.
- `GET /api/config` — legacy/full config view.
- `POST /api/config` — save config.
- `POST /api/secrets` — save secret values such as Gemini API key or gateway token.
- `GET /api/prompts` — read custom prompts.
- `POST /api/prompts` — save custom prompts.

Important: `GET /api/runtime` redacts saved SSID/password/API keys as `set`/`missing`. Do not round-trip redacted values back into `POST /api/runtime`; send only fields you intend to change.

## Gemini and voice

- `GET|POST /api/voice/toggle` — request wake/stop voice toggle.
- `GET|POST /api/gemini/text` — send text to Gemini path.
- `GET|POST /api/gemini/look` — disabled/stubbed in current tree; camera smoke tests remain available.

Example:

```bash
curl -u stackchan:YOUR_PASSWORD -X POST http://ROBOT_IP/api/gemini/text \
  -H 'Content-Type: application/json' \
  -d '{"text":"hello"}'
```

## Camera

- `GET /api/camera/status` — camera status.
- `GET|POST /api/camera/capture` — capture smoke test, returns metadata only.
- `GET /api/camera/jpeg` — latest/direct JPEG.
- `GET /api/camera/latest.jpg` — alias for JPEG.

## Servo and emotion

- `GET|POST /api/emotion` — read/set emotion.
- `GET /api/servo/status` — current servo status.
- `POST /api/servo/move` — bounded servo move.
- `GET|POST /api/servo/gesture` — run allowlisted gesture.

Servo move is bounded by firmware clamps. Use small moves and verify physical clearance.

## Memory

- `GET /api/memory/recent`
- `GET /api/memory/dialogues`
- `GET /api/memory/summaries`
- `GET|POST /api/memory/search`
- `GET /api/memory/stats`
- `GET|POST /api/memory/summary-config`
- `POST /api/memory/summarize`
- `POST /api/memory/vectorize`

## Gateway and sensors

- `GET /api/gateway/tools` — list gateway tools if configured.
- `GET /api/sensors` — sensor diagnostics.
- `GET /sensors` — browser diagnostics page.
