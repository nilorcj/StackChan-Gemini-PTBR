# StackChan Gemini PT-BR

🇧🇷 [Português](README.md) | 🇺🇸 [English](README_EN.md)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange.svg)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![M5Stack CoreS3](https://img.shields.io/badge/Hardware-M5Stack%20CoreS3-blue.svg)](https://m5stack.com/)
[![Gemini Live](https://img.shields.io/badge/AI-Google%20Gemini%20Live-4285F4.svg)](https://ai.google.dev/)

Advanced firmware for the **M5Stack CoreS3 Stack-chan** robot featuring real-time conversational voice interaction (**Google Gemini Live API**) in **Brazilian Portuguese (PT-BR)**, modular peripheral support for the **M5Stack Units** ecosystem, an intelligent gesture- and touch-armed listening state machine (*Gesture-Armed Listening*), advanced power management with capacitive touch wake, and external tool integration via a **ToolGateway authenticated with HMAC-SHA256**.

* **Official repository:** [https://github.com/nilorcj/StackChan-Gemini-PTBR](https://github.com/nilorcj/StackChan-Gemini-PTBR)
* **Recommended hardware setup:** M5Stack CoreS3 + Stack-chan Base + FAT32 MicroSD card.

---

## 🌟 1. Project Overview

**StackChan Gemini PT-BR** turns the M5Stack CoreS3 desktop robot into an expressive, physical conversational assistant. Leveraging the ultra-low-latency bidirectional voice capabilities of the **Google Gemini Live API**, the robot holds natural spoken dialogues in Brazilian Portuguese, displays procedural facial expressions, moves its head, and runs local and external tools.

### Origin and Attribution
This project was developed based on the initial experimental firmware by [taranton/stackchan-gemini-firmware](https://github.com/taranton/stackchan-gemini-firmware), which pioneered bidirectional Gemini Live voice streaming on the ESP32-S3.

Building upon that foundation, this repository introduces a comprehensive architectural evolution:
- **Full PT-BR localization** (persona, system prompts, spoken prompts, and web configuration interface);
- **Gesture-Armed Listening state machine**, eliminating accidental voice triggers caused by background room noise;
- **Modular integration with M5Stack Units** (PaHub, Laser ToF, ENV sensor, and Haptic Feedback);
- **Secure ToolGateway via Cloudflare Worker with HMAC-SHA256** for live web searches and personal calendar queries;
- **Deep power management with 15-minute automatic deep sleep** and capacitive touch wake;
- **Modern, responsive, distributable web configuration panel (WebView)**.

---

## 🚀 2. Key Features

- **Fluid Real-Time Conversation:** Continuous 16 kHz PCM audio streaming using `models/gemini-3.8-live` in Brazilian Portuguese, featuring low latency and natural barge-in interruption.
- **Conscious & Armed Listening:** The microphone never opens on random ambient noise. Listening is armed exclusively by waving in front of the Laser ToF sensor or tapping the touchscreen.
- **Physical Mute Button (Alexa-Style):** A quick click on the power button mutes the microphone and blocks conversations until toggled off.
- **Procedural Expressive Face:** Dynamic eyes and mouth rendered algorithmically on the CoreS3 display with blink animations, emotional states, and a quick status drawer (*swipe down* from the top of the screen).
- **Smooth Pan/Tilt Motion:** Kinematic servo motion for yaw and pitch using the Stack-chan motorized base.
- **Local On-Device Tools:**
  - `get_sensor_readings`: Reads desktop distance, temperature, humidity, and atmospheric pressure;
  - `get_current_datetime`: Official date and time synchronized via NTP/RTC;
  - `get_weather` & `get_currency`: Brasília weather and USD/BRL exchange rate served from local cache;
  - `get_news`: Headlines from local RSS cache;
  - `set_speaker_volume`: Physical speaker volume control;
  - `search_memory` & `remember_fact`: Long-term memory stored on the microSD card.
- **External Tools via ToolGateway:**
  - `web_search`: Live web search powered by the TinyFish API;
  - `get_agenda`: Calendar lookup from a private Google Calendar iCalendar feed.
- **Modern Web Configuration Panel:** Secure WebView on port 80 for Wi-Fi, AI, ToolGateway, audio, and memory management, including a built-in cryptographic HMAC key generator.

---

## 🧩 3. Required, Recommended, and Optional Hardware

> [!IMPORTANT]
> **External modules are NOT required for basic voice conversation.** The firmware is structured to allow conversational interaction with Gemini Live using only the **M5Stack CoreS3**, a **MicroSD card**, and your local Wi-Fi connection.

### Component Overview

| Component | Requirement | Operation Without Component |
|---|---|---|
| **M5Stack CoreS3** | **Required** | Central controller. Houses the ESP32-S3 SoC, capacitive touchscreen, dual microphones, speaker, camera, and power management circuitry. |
| **MicroSD Card** | **Required** *(in current implementation)* | Stores configuration (`runtime.json`), network/API secrets, prompts, and persistent memory. Without the card, the firmware does not initialize networking or the Gemini connection. Must be formatted in **FAT32**. |
| **Motorized Base / Servos** | **Recommended** | Provides physical pan/tilt expressiveness. If the base is not attached, the firmware continues booting after a 1.2-second IO expander timeout; voice, display, and audio routines remain fully active. *(Static desk-assistant operation is supported in software, but has not yet been physically bench-tested without the base attached).* |
| **Unit PaHub** | **Optional** | I2C multiplexer (PCA9548AP) on Port A. If absent, the firmware continues booting without blocking startup; additional I2C ports remain disabled. *(Confirmed in code).* |
| **Unit ToF (Laser)** | **Optional** | Measures distance and detects hand waves to arm listening. If absent, **conversations can be triggered at any time by tapping the touchscreen**. *(Confirmed in code).* |
| **Unit ENV (III/IV)** | **Optional** | Measures local temperature, humidity, and pressure. If absent, `get_sensor_readings` returns a handled error; online weather forecasts remain operational. *(Confirmed in code).* |
| **Unit Vibrator (U059)** | **Optional** | Haptic motor on Port B (GPIO 9) for tactile feedback (purring, clicks). If absent, PWM output is harmless and the robot functions normally. *(Confirmed in code).* |

### Minimal Working Setup
The simplest functional setup to get the robot talking consists of:
1. **M5Stack CoreS3** powered via USB-C;
2. **FAT32 MicroSD card** inserted into the CoreS3;
3. **2.4 GHz Wi-Fi network** with internet access;
4. **Google Gemini API Key** (from Google AI Studio).

In this compact mode, with no sensors or servos attached, conversation is triggered by tapping the CoreS3 screen.

---

## 📦 4. Firmware Installation

You can install the firmware using pre-compiled binaries or by building from source using PlatformIO.

### Method A: Pre-Compiled Firmware (GitHub Release)

> [!NOTE]
> **Release Availability and Visibility:** Official release `v1.0.0` is undergoing documentation preparation and is not available for download prior to its formal publication on the [GitHub Releases page](https://github.com/nilorcj/StackChan-Gemini-PTBR/releases). Furthermore, while the repository remains private, release pages and downloadable binaries are only accessible to authorized accounts with explicit access to the repository.

The ESP32-S3 requires 4 distinct binary images flashed at specific memory offsets for its 16 MB partition layout. Flashing only `firmware.bin` to a clean or previously overwritten board will fail to boot because the matching partition table and bootloader must be present:

| File | Flash Offset | Description |
|---|---|---|
| `bootloader.bin` | `0x0000` | ESP32-S3 2nd-stage bootloader |
| `partitions.bin` | `0x8000` | Partition table configured for 16 MB |
| `boot_app0.bin` | `0xe000` | OTA boot selector (standard Espressif) |
| `firmware.bin` | `0x10000` | Application image for StackChan Gemini PT-BR |

When release binaries are made available, flash them using the official `esptool.py` utility:

#### Universal single-line command (Windows PowerShell, CMD, Linux, macOS):
```bash
esptool.py --chip esp32s3 --port SERIAL_PORT --baud 115200 write_flash -z 0x0000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

#### Windows (PowerShell):
```powershell
esptool.py --chip esp32s3 --port COM3 --baud 115200 write_flash -z `
  0x0000 bootloader.bin `
  0x8000 partitions.bin `
  0xe000 boot_app0.bin `
  0x10000 firmware.bin
```

#### Windows (Command Prompt / CMD):
```cmd
esptool.py --chip esp32s3 --port COM3 --baud 115200 write_flash -z ^
  0x0000 bootloader.bin ^
  0x8000 partitions.bin ^
  0xe000 boot_app0.bin ^
  0x10000 firmware.bin
```

#### Linux / macOS (Bash):
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 115200 write_flash -z \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin
```
*(See [docs/BUILD_FLASH.md](docs/BUILD_FLASH.md) for further details).*

### Method B: Building from Source with PlatformIO
To build directly from source:
1. Install [PlatformIO Core](https://platformio.org/) or the VS Code extension.
2. Clone the official repository:
   ```bash
   git clone https://github.com/nilorcj/StackChan-Gemini-PTBR.git
   cd StackChan-Gemini-PTBR
   ```
3. Connect the CoreS3 via USB-C and build/upload in a single step:
   ```bash
   pio run -e m5stack-cores3 -t upload
   ```
   *(PlatformIO downloads all library dependencies and flashes the correct offsets automatically).*

---

## 🛠️ 5. Initial Setup

A FAT32-formatted MicroSD card must be inserted into the CoreS3 slot. You can perform initial configuration via either of the following methods:

### Option 1: Guided Setup via WebView (SoftAP Mode)
This is the most straightforward method, as it does not require editing configuration files on a PC:
1. Insert a FAT32-formatted MicroSD card into the CoreS3 (it can be empty);
2. Power on the CoreS3. Since no Wi-Fi credentials exist, the robot waits 15 seconds on the boot screen and launches a configuration access point:
   - **SSID:** `<robot_id>-setup-XXXX` (e.g., `stackchan-setup-XXXX` or `pix-setup-XXXX`, where `XXXX` represents the last 4 hex characters of the board's MAC address);
   - **Password:** `stackchanXXXX`;
3. Connect your phone or computer to the robot's Wi-Fi network and open in a browser:
   ```text
   http://192.168.4.1/
   ```
4. In the **Configuração** (Settings) tab, enter your 2.4 GHz Wi-Fi SSID and Password in card **1. Wi-Fi** and your API key in card **2. Gemini Live**;
5. Click **Salvar e reiniciar** (Save and Reboot). The firmware automatically creates the folders and writes the configuration files to the MicroSD card, rebooting the robot connected to your home network.

### Option 2: Manual File Preparation on PC
To prepare the card on your computer before powering up:
1. Connect the MicroSD card to your PC;
2. Create `/app/StackChan/config/runtime.json`:
   ```json
   {
     "robot_id": "pix",
     "wifi_enabled": true,
     "wifi_ssid": "YOUR_WIFI_SSID",
     "web_enabled": true,
     "gemini_enabled": true,
     "gemini_model": "models/gemini-3.8-live",
     "gemini_voice": "Puck",
     "gateway_enabled": false
   }
   ```
3. Create `/app/StackChan/secrets/` with plain-text files (no trailing newlines or extra spaces):
   - `/app/StackChan/secrets/wifi_password.txt` containing your Wi-Fi password;
   - `/app/StackChan/secrets/gemini_api_key.txt` containing your Gemini API key.
4. Insert the card into the CoreS3 and power it on.

*(Refer to [docs/SD_LAYOUT.md](docs/SD_LAYOUT.md) for the full directory tree reference).*

---

## 🌐 6. Wi-Fi Configuration and WebView Access

### Normal Operation vs. Fallback Access Point (SoftAP)
- **Normal Mode:** On boot, the CoreS3 displays a diagnostics dashboard for 15 seconds while connecting to your home Wi-Fi network. Once connected, it displays its assigned local IP address (e.g., `192.168.1.105`).
- **SoftAP Mode (Fallback):** If the configured Wi-Fi is not reachable within 15 seconds, or if credentials are missing, the robot starts its own temporary Wi-Fi network:
  - **Network Name (SSID):** `<robot_id>-setup-XXXX` (e.g., `stackchan-setup-XXXX` or `pix-setup-XXXX`);
  - **Password:** `stackchanXXXX` (the word `stackchan` in lowercase followed by the same 4 uppercase hex digits from the SSID);
  - **IP Address:** `http://192.168.4.1/`.

> [!NOTE]
> **Wi-Fi Compatibility:** The ESP32-S3 strictly requires **2.4 GHz** networks with **WPA/WPA2-Personal (PSK)** authentication. 5 GHz bands and corporate WPA2/WPA3-Enterprise networks (802.1X with user certificates/domain logins, such as eduroam) are **not supported**.

### Accessing the Web Panel (WebView)
With the robot and your computer/phone connected to the same local network:
- Access via browser: `http://<ROBOT_IP>/` (e.g., `http://192.168.1.105/`).
- Access is always performed using the numeric IP address (mDNS / `.local` domain resolution is not used).

### Modern WebView Layout
The web interface features three main tabs:
1. **Status:** Live telemetry (battery voltage and percentage, RSSI, Gemini Live connection state, dialogue turns, free Heap/PSRAM, sensors, and cached news headlines);
2. **Configuração (Configuration):** Settings organized into modular cards:
   - **1. Wi-Fi:** Enable toggle, SSID, and password with a reveal button;
   - **2. Gemini Live:** Enable toggle, runtime model selection, voice, and protected API key field;
   - **3. ToolGateway:** Enable toggle, HTTPS Worker URL, and HMAC-SHA256 secret management (with "Gerar nova chave" and "Copiar" buttons);
   - **4. Energia e repouso (Power & Sleep):** Visual summary of validated sleep and deep sleep cycles;
   - **5. Áudio & Fala (Audio & Speech):** Master volume and fine-tuning for mic gain and VAD parameters;
   - **6. Segurança Web & Sistema (Security & System):** Timezone offset, robot identifier, and web access password;
   - **7. Prompts Adicionais (Prompts):** System and persona overlays to tune dialogue behavior.
   - **Actions:** Prominent "Salvar configurações" (Save) and "Salvar e reiniciar" (Save & Reboot) buttons (blank secret fields preserve existing values).
3. **Memória (Memory):** Inspection of recent dialogues, periodic summaries, and semantic fact lookups stored on the MicroSD.

---

## 🤖 7. Gemini Live and API Configuration

- **Obtaining an API Key:** Get an API key from [Google AI Studio](https://aistudio.google.com/) (subject to Google's terms of service, tier availability, and quota limits).
- **Default Validated Model:** The firmware natively targets `models/gemini-3.8-live` with the `Puck` voice, validated in hardware for audio stability and low response latency.
- **Free Tier Compatibility:** The connection setup routine was tailored to remove the legacy native Google Search Grounding parameter. In tested environments, this adjustment allows evaluation/test keys on Google's Free Tier to connect successfully (`connected=true`, `ready=true`). *(Note: quota limits, requests per minute, and tier availability are governed entirely by Google and may change over time).*
- **Starting a Conversation:**
  - **By Screen Tap:** Tap anywhere on the CoreS3 display to arm listening;
  - **By Hand Wave:** Make a brisk waving gesture 15 to 30 cm in front of the Laser ToF sensor.
- **Worker Independence:** Gemini Live functions autonomously for spoken dialogue without requiring any external gateway. The ToolGateway is needed only if web search or Google Calendar queries are desired.

---

## ☁️ 8. ToolGateway, Cloudflare Worker, TinyFish, and Google Calendar

To provide live external information, the firmware communicates with an external **Cloudflare Worker** acting as a **ToolGateway**:

```text
Pix (CoreS3) ──[POST /v2/call + HMAC-SHA256]──> Cloudflare Worker ──┬──> TinyFish API (web_search)
                                                                    └──> Google Calendar ICS (get_agenda)
```

### Cryptographic HMAC-SHA256 Authentication
All requests to the Worker on `POST /v2/call` require:
- `X-Timestamp`: Unix timestamp in seconds (validity window: ±120s);
- `X-Nonce`: 32 random hexadecimal characters;
- `X-Signature`: HMAC-SHA256 signature calculated over the HTTP method, path, timestamp, nonce, and JSON body.

### Setting Up Your Own Worker:
1. Navigate to the `cloudflare-worker/` directory in this repository;
2. Follow the deployment guide in [cloudflare-worker/README.md](cloudflare-worker/README.md) to deploy the Worker using the Cloudflare dashboard or Wrangler CLI (compatible with standard/free Cloudflare plans within daily platform request limits);
3. Set the three required secrets in the Worker environment:
   - `HMAC_SHARED_KEY`: 64-character lowercase hex secret;
   - `TINYFISH_API_KEY`: API key for web search from [TinyFish](https://tinyfish.ai/) (subject to provider terms);
   - `CALENDAR_ICS_URL`: Private HTTPS feed URL (`https://...`) of your Google Calendar in `.ics` format.
4. In the robot's WebView (**Configuração** > card **3. ToolGateway**):
   - Enable **Habilitar ToolGateway**;
   - Enter your Worker URL in **URL Base do ToolGateway** (e.g., `https://pix-tool-gateway.your-account.workers.dev`);
   - Enter the matching **Chave Secreta HMAC-SHA256**;
   - Click **Salvar configurações**.

---

## 🔒 9. Architecture, Security, and Memory

- **Local vs. Remote Separation:** Real-time audio, sensor capture, and servo kinematics run locally on the ESP32-S3. Only tasks requiring heavy text parsing (such as `RRULE` expansion on large calendars or web scraping) are offloaded to the Worker.
- **Secrets Kept Out of Git:** API keys, Wi-Fi passwords, and calendar URLs are never stored in source code. They reside solely on the physical MicroSD card or inside encrypted Cloudflare Worker environment variables.
- **Local Network Isolation:** The robot's web server is intended for trusted local area networks (LAN). Never expose the robot's HTTP port directly to the open internet without an authentication layer and VPN/firewall.
- **Local Memory:** Dialogue logs and summaries are stored under `/app/StackChan/memory/` on the MicroSD. Clear this folder before handing the card or robot to another person.

---

## ⚡ 10. Power Management and Sensors

The CoreS3 features a two-stage power management system, verified in hardware:

1. **Visual Sleep (~2 minutes of inactivity):**
   - The robot closes its eyes and enters a light sleeping animation (`sleep`);
   - Audio and network sockets remain alive;
   - Wakes immediately upon detecting a touch or presence on the ToF sensor.
2. **Automatic Deep Sleep (15 minutes without real human interaction):**
   - After 15 continuous minutes without speech or physical touch, the robot enters **Deep Sleep** to conserve battery and hardware life;
   - Cuts power to the servo rail (5V boost disabled);
   - Gracefully disconnects the Gemini Live session;
   - Powers off LEDs, audio amp, and the Wi-Fi radio;
   - Suspends the ESP32-S3 CPU via:
     ```cpp
     M5.Power.deepSleep(M5.Power.sleep_no_timer, true);
     ```
3. **Capacitive Touch Wake:**
   - Touching the CoreS3 display triggers an external interrupt (`ESP_SLEEP_WAKEUP_EXT1`), waking the device immediately;
   - The robot performs a fast boot, reconnects to Wi-Fi and Gemini Live, and resumes interaction.

---

## 💻 11. Building from Source

To build, audit, or customize the firmware:

```bash
# 1. Install PlatformIO
pip install -U platformio

# 2. Clone repository
git clone https://github.com/nilorcj/StackChan-Gemini-PTBR.git
cd StackChan-Gemini-PTBR

# 3. Build firmware
pio run -e m5stack-cores3

# 4. Upload to CoreS3 via USB
pio run -e m5stack-cores3 -t upload

# 5. Monitor serial logs
pio device monitor -b 115200
```

---

## ⚠️ 12. Known Limitations

- **Wi-Fi Networks:** Strictly limited to **2.4 GHz** with **WPA-PSK / WPA2-PSK**. 5 GHz and enterprise networks (WPA2-Enterprise 802.1X) are not supported by the hardware.
- **Local Name Resolution (mDNS):** Web panel access requires the numeric IP address (`http://192.168.x.x/`); local `.local` domain names are not supported.
- **MicroSD Card Dependency:** A FAT32-formatted MicroSD card must be present for credentials, settings, and networking to initialize.
- **Project Nature:** Open-source project intended for developers, students, and robotics/AI hobbyists.

---

## 📚 13. Additional Documentation, Credits, and License

### Complementary Guides
- [Build and Flash Details](docs/BUILD_FLASH.md)
- [MicroSD Directory Layout](docs/SD_LAYOUT.md)
- [Firmware Feature Matrix](docs/FEATURES.md)
- [Cloudflare Worker Setup](cloudflare-worker/README.md)
- [Web Security Guide](docs/WEB_SECURITY.md)
- [HTTP API Reference](docs/API.md)
- [Local Memory Architecture](docs/MEMORY_INTERNALS.md)

### Credits
- Reference project: [taranton/stackchan-gemini-firmware](https://github.com/taranton/stackchan-gemini-firmware);
- Original Stack-chan concept by [shakao / robo-stackchan](https://github.com/stack-chan/stack-chan);
- Developed and maintained by [@nilorcj](https://github.com/nilorcj).

### License
Distributed under the **MIT** License. See [LICENSE](LICENSE) for full terms and conditions.
