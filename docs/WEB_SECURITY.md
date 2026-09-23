# Web UI/API security

The firmware includes lightweight LAN protection for the Web UI/API.

## Auth model

- HTTP Basic Auth.
- Username: `stackchan`.
- Password: configured through the Web UI **Web security** section or `POST /api/security`.
- Stored as SHA-256 at `/app/StackChan/secrets/web_password_sha256.txt`.
- Password is not stored as plaintext by the firmware.

## Bootstrap behavior

To avoid locking out a fresh device:

- If no Web password hash exists, the Web UI/API is open. This includes first setup access point mode.
- `POST /api/security` can set the first password without auth.
- Once a password exists, changing it requires auth.
- Most routes require auth after the password is configured.

## Set password with curl

First-time setup:

```bash
curl -X POST http://ROBOT_IP/api/security \
  -H 'Content-Type: application/json' \
  -d '{"web_password":"CHANGE_ME"}'
```

Authenticated request after setup:

```bash
curl -u stackchan:CHANGE_ME http://ROBOT_IP/api/status
```

Change password after setup:

```bash
curl -u stackchan:OLD_PASSWORD -X POST http://ROBOT_IP/api/security \
  -H 'Content-Type: application/json' \
  -d '{"web_password":"NEW_PASSWORD"}'
```

## Threat model

This is LAN hardening only:

- Basic Auth is simple and embedded-friendly.
- Do not expose the robot Web UI directly to the public Internet.
- Prefer a trusted LAN/VPN if remote access is needed.
- Set a Web password immediately after first setup, especially before joining shared Wi-Fi networks.
- Treat SD-card physical access as trusted access.
