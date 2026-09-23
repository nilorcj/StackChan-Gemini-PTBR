#include <Arduino.h>
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#include <M5Unified.h>
#include <M5StackChan.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include "GeminiLiveProbe.h"
#include "MemoryStore.h"
#include "ToolGatewayClient.h"
#include "GeminiToolBridge.h"
#include "WebConfigServer.h"
#include "ConfigManager.h"
#include "EmotionController.h"
#include "ServoGestureController.h"
#include "CameraCapture.h"
#include "UnitPaHubManager.h"
#include "LiveFeedsManager.h"
#include "GoogleCalendarClient.h"
#include "TofGestureDetector.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#endif

GeminiLiveProbe gemini;
MemoryStore memory(SD);
TofGestureDetector g_tofDetector;
ToolGatewayClient toolGateway;
EmotionController emotion;
ServoGestureController servoGestures;
CameraCapture camera;
UnitPaHubManager unitPaHub(UnitPaHubManager::kPaHubDefaultI2CAddr, UnitPaHubManager::kDefaultVibratorPin);
LiveFeedsManager liveFeeds;
GeminiToolBridge toolBridge(toolGateway, emotion, servoGestures, camera);
WebConfigServer webConfig(SD, memory, toolGateway, emotion, servoGestures, gemini, camera);
ConfigManager configManager(SD);
GoogleCalendarClient googleCalendar(configManager);
volatile bool g_voice_toggle_requested = false;
static uint8_t g_speaker_volume = 200;
static constexpr uint32_t kIdleSleepMs = 120000; // 2 minutes desk idle before sleep (prevents premature sleeping)
static constexpr uint32_t kPowerButtonLedOffHoldMs = 700;
static uint32_t g_last_human_activity_ms = 0;
static uint32_t g_power_button_pressed_ms = 0;
static bool g_power_button_leds_off = false;
static uint32_t g_last_unit_scan_ms = 0;
static uint32_t g_last_sensor_read_ms = 0;
static uint16_t g_current_distance_mm = 0;
static uint32_t g_last_tof_fast_ms = 0;
static uint32_t g_last_hand_swipe_ms = 0;
static bool g_tof_presence = false;
static uint32_t g_last_presence_reaction_ms = 0;
static uint32_t g_last_air_pet_ms = 0;
static uint32_t g_emotion_override_until_ms = 0;
static float g_latest_temp = 0.0f;
static float g_latest_hum = 0.0f;
static float g_latest_pressure = 0.0f;
static float g_last_pressure = 0.0f;
static uint32_t g_last_breeze_ms = 0;
static uint32_t g_last_als_read_ms = 0;
static uint32_t g_darkness_start_ms = 0;
static bool g_night_mode = false;
static bool g_physical_mute = false;
static float g_last_humidity = 0.0f;
static uint32_t g_last_idle_breath_ms = 0;

static const char* const kLiftPhrases[] = {
  "Ei, me coloca no chão! Não gosto de altura!",
  "Uou! Tiraram o chão dos meus pezinhos!",
  "Ei, ei, ei! Pra onde você tá me levando?",
  "Socorro, tô voando! Me bota na mesa!"
};

static const char* const kBlowPhrases[] = {
  "Nossa, que ventinho gostoso e refrescante!",
  "Uau, que brisa boa! Tava precisando esfriar meus circuitos!",
  "Brrr, que friozinho bom! Adorei esse ventinho!",
  "Eita, de onde veio essa brisa repentina? Que delícia!"
};

static const char* const kClosePhrases[] = {
  "Epa! Você tá muito perto, quase colou no meu nariz!",
  "Uou! Que susto, você apareceu de repente na minha frente!",
  "Eita! Cuidado com a colisão frontal, chega um pouquinho pra trás!",
  "Caramba, que susto! Você tá gigante na minha frente!"
};

static const char* const kTableReliefPhrases[] = {
  "Ufa, que alívio! De volta ao chão firme!",
  "Ah, obrigado por me colocar de volta na mesa!",
  "Ufa! Meus circuitos agradecem a segurança da mesa!",
  "Ainda bem! Ficar pendurada no ar dá um frio na espinha!"
};

static void speakFixedReaction(const char* eventName, const char* const phrases[], size_t count) {
  if (!gemini.isReady() || gemini.isDoNotDisturb()) return;

  if (gemini.isTurnBusy()) {
    Serial.printf("SensorySpeech [%s]: blocked/deferred (turn busy: state=%s speaking=%d debounce=%d)\n",
                  eventName ? eventName : "SENSORY",
                  gemini.turnStateString(),
                  gemini.isSpeaking() ? 1 : 0,
                  gemini.isInPostSpeechDebounce() ? 1 : 0);
    return;
  }

  size_t idx = random(0, count);
  String turn = "[SISTEMA SENSORIAL - ";
  turn += eventName;
  turn += "]: Diga exatamente esta frase em voz alta com muita expressividade e emoção: \"";
  turn += phrases[idx];
  turn += "\"";
  Serial.printf("SensorySpeech [%s]: %s\n", eventName, phrases[idx]);
  gemini.requestTextTurn(turn);
}


static void applySpeakerVolume() {
  M5.Speaker.setVolume(g_speaker_volume);
  M5.Speaker.setAllChannelVolume(g_speaker_volume);
  gemini.setSpeakerVolume(g_speaker_volume);
}

static void playBootDroidWhistle() {
  // Silenced: robot starts cleanly without any loud/harsh whistle
  Serial.println("Boot: boot whistle silenced for clean startup");
}

static void playWakeDroidChirp() {
  static constexpr int kWakeSoundSampleRate = 24000;
  static constexpr float kPi = 3.14159265358979f;
  static constexpr float kTwoPi = 2.0f * kPi;
  static int16_t wakeSound[4000];
  static uint32_t lastWakeChirpMs = 0;

  uint32_t now = millis();
  if (now - lastWakeChirpMs < 4000) return;  // debounce future proximity/touch bursts.
  lastWakeChirpMs = now;

  if (gemini.isSpeaking() || gemini.isRecording() || gemini.i2sOwner() == GeminiLiveProbe::I2SOwner::MIC_OWNER || !M5.Speaker.isRunning() || M5.Speaker.isPlaying(1)) return; // Do not collide with voice/mic

  size_t pos = 0;
  auto appendTone = [&](float freq, float dur, float amp = 0.22f) {
    int n = static_cast<int>(kWakeSoundSampleRate * dur);
    float phase = 0.0f;
    for (int i = 0; i < n && pos < (sizeof(wakeSound) / sizeof(wakeSound[0])); ++i) {
      float t = static_cast<float>(i) / static_cast<float>(n);
      float env = sinf(t * kPi);
      float s = amp * env * sinf(phase);
      wakeSound[pos++] = static_cast<int16_t>(32767.0f * s);
      phase += kTwoPi * freq / static_cast<float>(kWakeSoundSampleRate);
      if (phase > kTwoPi) phase -= kTwoPi;
    }
  };

  // Soft, warm gentle 2-tone melodic cue instead of a harsh whistle
  appendTone(523.25f, 0.07f, 0.20f); // C5
  appendTone(659.25f, 0.09f, 0.22f); // E5

  const uint8_t savedVolume = g_speaker_volume;
  const uint8_t effectVolume = savedVolume > 180 ? 60 : (savedVolume > 80 ? (savedVolume * 5) / 16 : 30);
  static constexpr uint8_t kEffectChannel = 0;
  M5.Speaker.setChannelVolume(kEffectChannel, effectVolume);
  bool queued = M5.Speaker.playRaw(wakeSound, pos, kWakeSoundSampleRate, false, 1, kEffectChannel, false);
  Serial.printf("Wake soft chime: samples=%u volume=%u channel=%u queued=%s\n",
                static_cast<unsigned>(pos), static_cast<unsigned>(effectVolume),
                static_cast<unsigned>(kEffectChannel), queued ? "yes" : "no");
}

static void playLiftSurprisedWhistle() {
  static constexpr int kSampleRate = 24000;
  static constexpr float kPi = 3.14159265358979f;
  static constexpr float kTwoPi = 2.0f * kPi;
  static int16_t soundBuf[8000];
  static uint32_t lastLiftSoundMs = 0;
  uint32_t now = millis();
  if (now - lastLiftSoundMs < 2500) return;
  lastLiftSoundMs = now;

  if (gemini.isSpeaking() || gemini.isRecording() || gemini.i2sOwner() == GeminiLiveProbe::I2SOwner::MIC_OWNER || !M5.Speaker.isRunning()) return;

  size_t pos = 0;
  auto appendSweep = [&](float f1, float f2, float dur, float amp = 0.48f) {
    int n = static_cast<int>(kSampleRate * dur);
    float phase = 0.0f;
    for (int i = 0; i < n && pos < (sizeof(soundBuf) / sizeof(soundBuf[0])); ++i) {
      float t = static_cast<float>(i) / static_cast<float>(n);
      float f = f1 + (f2 - f1) * t;
      float env = sinf(t * kPi);
      float s = amp * env * sinf(phase);
      soundBuf[pos++] = static_cast<int16_t>(32767.0f * s);
      phase += kTwoPi * f / static_cast<float>(kSampleRate);
      if (phase > kTwoPi) phase -= kTwoPi;
    }
  };

  appendSweep(700.0f, 1500.0f, 0.10f, 0.45f);
  appendSweep(1500.0f, 2600.0f, 0.15f, 0.50f);

  const uint8_t savedVolume = g_speaker_volume;
  const uint8_t effectVolume = savedVolume > 180 ? 110 : (savedVolume > 80 ? (savedVolume * 10) / 16 : 55);
  static constexpr uint8_t kEffectChannel = 0;
  M5.Speaker.setChannelVolume(kEffectChannel, effectVolume);
  M5.Speaker.playRaw(soundBuf, pos, kSampleRate, false, 1, kEffectChannel, false);
}

static void playCatPurrDocileSound() {
  static constexpr int kSampleRate = 24000;
  static constexpr float kPi = 3.14159265358979f;
  static constexpr float kTwoPi = 2.0f * kPi;
  static int16_t soundBuf[8000];
  static uint32_t lastPurrSoundMs = 0;
  uint32_t now = millis();
  if (now - lastPurrSoundMs < 2000) return;
  lastPurrSoundMs = now;

  if (gemini.isSpeaking() || gemini.isRecording() || gemini.i2sOwner() == GeminiLiveProbe::I2SOwner::MIC_OWNER || !M5.Speaker.isRunning()) return;

  size_t pos = 0;
  auto appendSweep = [&](float f1, float f2, float dur, float amp = 0.42f) {
    int n = static_cast<int>(kSampleRate * dur);
    float phase = 0.0f;
    for (int i = 0; i < n && pos < (sizeof(soundBuf) / sizeof(soundBuf[0])); ++i) {
      float t = static_cast<float>(i) / static_cast<float>(n);
      float f = f1 + (f2 - f1) * t;
      float env = sinf(t * kPi);
      float s = amp * env * sinf(phase);
      soundBuf[pos++] = static_cast<int16_t>(32767.0f * s);
      phase += kTwoPi * f / static_cast<float>(kSampleRate);
      if (phase > kTwoPi) phase -= kTwoPi;
    }
  };

  // Sweet purring melody: soft gentle chimes expressing docile affection
  appendSweep(900.0f, 1500.0f, 0.10f, 0.38f);
  appendSweep(1500.0f, 2100.0f, 0.12f, 0.42f);
  appendSweep(2100.0f, 1700.0f, 0.14f, 0.36f);

  const uint8_t savedVolume = g_speaker_volume;
  const uint8_t effectVolume = savedVolume > 180 ? 95 : (savedVolume > 80 ? (savedVolume * 9) / 16 : 45);
  static constexpr uint8_t kEffectChannel = 0;
  M5.Speaker.setChannelVolume(kEffectChannel, effectVolume);
  M5.Speaker.playRaw(soundBuf, pos, kSampleRate, false, 1, kEffectChannel, false);
}

static void configureAudio(uint8_t micMagnification = 8, uint8_t micNoiseFilterLevel = 0) {
  if (micMagnification < 1) micMagnification = 1;
  if (micMagnification > 24) micMagnification = 24;
  if (micNoiseFilterLevel > 4) micNoiseFilterLevel = 4;

  auto mic = M5.Mic.config();
  mic.magnification = micMagnification;
  mic.noise_filter_level = micNoiseFilterLevel;
  M5.Mic.config(mic);
}

static void drawStatus(const char* line) {
  // Keep the LCD owned by EmotionController's procedural face renderer.
  // Older builds wrote status text directly to the screen here, which briefly
  // replaced the face until the next face frame. Status now goes to serial/API.
  Serial.printf("Status: %s\n", line ? line : "");
}

static void blackoutExternalLightsForPowerOff() {
  if (!g_power_button_leds_off) {
    Serial.println("Power: button hold -> RGB off latch");
    g_power_button_leds_off = true;
  }
  // The StackChan RGB LEDs live on the BSP IO expander and can remain latched
  // even when the CoreS3 display/PMIC turns the screen off. Keep writing black
  // while the firmware still runs so EmotionController cannot relight them.
  M5StackChan.showRgbColor(0, 0, 0);
}

static bool connectWifiFromConfig(const ConfigManager& cfg) {
  const auto& c = cfg.config();
  if (!c.wifiEnabled) {
    Serial.println("WiFi: disabled");
    return false;
  }
  String password = cfg.readWifiPassword();
  if (!c.wifiSsid.length() || !password.length()) {
    Serial.println("WiFi: missing ssid or password");
    return false;
  }
  Serial.println("WiFi: starting station mode");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  // DHCP on some routers/boots can leave DNS unusable even though the ESP is
  // reachable on the LAN. Gemini Live connect then fails at hostByName() and
  // the robot drops into error before audio starts. Keep DHCP addressing, but
  // pin reliable resolvers for outbound Gemini/gateway connections.
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE,
              IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
  WiFi.begin(c.wifiSsid.c_str(), password.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    M5StackChan.update();
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false); // Disable WiFi power save to eliminate packet jitter on audio stream
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
                IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
    Serial.print("WiFi: connected ip=");
    Serial.print(WiFi.localIP());
    Serial.print(" dns=");
    Serial.println(WiFi.dnsIP());
    drawStatus(WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("WiFi: connect timeout");
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  return false;
}

static bool shouldStartSetupAccessPoint(const ConfigManager& cfg, bool sdConfigExists) {
  const auto& c = cfg.config();
  if (!sdConfigExists) return true;
  if (c.wifiEnabled) return true;
  return false;
}

static String setupApSuffix() {
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFF));
  return String(suffix);
}

static bool startSetupAccessPoint(const ConfigManager& cfg) {
  String suffix = setupApSuffix();
  String apName = cfg.config().robotId.length() ? cfg.config().robotId : "stackchan";
  apName += "-setup-";
  apName += suffix;
  apName.replace(" ", "-");
  String apPassword = "stackchan" + suffix;
  Serial.print("WiFi: starting setup access point ssid=");
  Serial.println(apName);
  Serial.print("WiFi: setup access point password=");
  Serial.println(apPassword);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  bool ok = WiFi.softAP(apName.c_str(), apPassword.c_str());
  if (!ok) {
    Serial.println("WiFi: setup access point failed");
    return false;
  }
  Serial.print("WiFi: setup access point ip=");
  Serial.println(WiFi.softAPIP());
  drawStatus(WiFi.softAPIP().toString().c_str());
  return true;
}

static bool initLtr553() {
  uint8_t dummy = 0;
  bool ok = true;
  dummy = 0x00;
  ok = ok && M5.In_I2C.writeRegister(0x23, 0x81, &dummy, 1, 100000L); // PS standby
  ok = ok && M5.In_I2C.writeRegister(0x23, 0x80, &dummy, 1, 100000L); // ALS standby
  dummy = 0x27;
  ok = ok && M5.In_I2C.writeRegister(0x23, 0x82, &dummy, 1, 100000L); // LED 40kHz 100mA
  dummy = 0x01;
  ok = ok && M5.In_I2C.writeRegister(0x23, 0x83, &dummy, 1, 100000L); // 1 pulse
  dummy = 0x00;
  ok = ok && M5.In_I2C.writeRegister(0x23, 0x84, &dummy, 1, 100000L); // 50ms PS
  dummy = 0x19;
  ok = ok && M5.In_I2C.writeRegister(0x23, 0x80, &dummy, 1, 100000L); // ALS active gain 48x
  dummy = 0x09;
  ok = ok && M5.In_I2C.writeRegister(0x23, 0x85, &dummy, 1, 100000L); // 100ms ALS
  dummy = 0x02;
  ok = ok && M5.In_I2C.writeRegister(0x23, 0x81, &dummy, 1, 100000L); // PS active
  Serial.printf("LTR-553ALS init: %s\n", ok ? "ok" : "failed");
  return ok;
}

static bool readLtr553Als(uint16_t& alsAvg) {
  uint8_t buf[4] = {0};
  if (!M5.In_I2C.readRegister(0x23, 0x88, buf, 4, 100000L)) return false;
  uint16_t ch1 = (static_cast<uint16_t>(buf[1]) << 8) | buf[0];
  uint16_t ch0 = (static_cast<uint16_t>(buf[3]) << 8) | buf[2];
  alsAvg = (ch0 + ch1) / 2;
  return true;
}

static void showBootDiagnosticScreen(uint32_t durationMs = 10000) {
  Serial.printf("BootDiag: displaying %u ms diagnostic screen (touch screen to skip)...\n", static_cast<unsigned>(durationMs));
  auto& display = M5StackChan.Display();
  display.fillScreen(TFT_BLACK);

  // Header Bar
  display.fillRect(0, 0, 320, 24, 0x0112); // Deep Navy
  display.drawLine(0, 24, 320, 24, 0x037A);
  display.setTextDatum(textdatum_t::middle_left);
  display.setTextSize(1);
  display.setTextColor(0x3DFE); // Electric Cyan
  display.drawString("PIX // DIAGNOSTICO DE SISTEMA", 8, 12);
  display.setTextDatum(textdatum_t::middle_right);
  display.setTextColor(0xFE44); // Amber/Gold
  display.drawString("M5 CORES3", 312, 12);

  // Panel 1: Conectividade (y: 28, h: 52)
  display.fillRoundRect(4, 28, 312, 52, 5, 0x0841);
  display.drawRoundRect(4, 28, 312, 52, 5, 0x1948);

  // Panel 2: Sensores PaHub (y: 84, h: 62)
  display.fillRoundRect(4, 84, 312, 62, 5, 0x0841);
  display.drawRoundRect(4, 84, 312, 62, 5, 0x1948);

  // Panel 3: Hardware CoreS3 & Bateria (y: 150, h: 48)
  display.fillRoundRect(4, 150, 312, 48, 5, 0x0841);
  display.drawRoundRect(4, 150, 312, 48, 5, 0x1948);

  uint32_t startMs = millis();
  uint32_t lastSensorUpdateMs = 0;
  uint16_t distMm = 0;
  UnitPaHubManager::EnvData env;

  while (millis() - startMs < durationMs) {
    M5StackChan.update();
    uint32_t now = millis();
    uint32_t elapsed = now - startMs;
    uint32_t remaining = (elapsed < durationMs) ? (durationMs - elapsed) : 0;
    uint32_t remainingSec = (remaining + 999) / 1000;

    // Check touch to skip (ignore dirty initial touch / noise during first 1500ms)
    auto t = M5.Touch.getDetail();
    if (elapsed > 1500 && t.wasClicked()) {
      Serial.println("BootDiag: skipped by user touch click");
      unitPaHub.click();
      break;
    }

    // Refresh live sensor values every 250ms
    if (now - lastSensorUpdateMs >= 250) {
      lastSensorUpdateMs = now;
      if (unitPaHub.isFound()) {
        unitPaHub.readSensors(distMm, env);
        if (env.valid) {
          g_latest_temp = env.temperature;
          g_latest_hum = env.humidity;
          g_latest_pressure = env.pressure;
        }
      }

      // Panel 1: Wi-Fi & Gemini Live
      display.setTextDatum(textdatum_t::middle_left);
      display.setTextSize(1);
      display.fillRect(10, 32, 300, 20, 0x0841);
      if (WiFi.status() == WL_CONNECTED) {
        display.setTextColor(0x07E0); // Green
        display.drawString("[WI-FI OK] ", 10, 42);
        display.setTextColor(TFT_WHITE);
        String wifiInfo = WiFi.SSID() + " (" + String(WiFi.RSSI()) + "dBm) " + WiFi.localIP().toString();
        display.drawString(wifiInfo.c_str(), 80, 42);
      } else {
        display.setTextColor(0xF800); // Red
        display.drawString("[WI-FI OFF] ", 10, 42);
        display.setTextColor(0xFDA0);
        display.drawString("Desconectado / Modo Setup AP", 88, 42);
      }

      display.fillRect(10, 54, 300, 20, 0x0841);
      if (configManager.hasGeminiApiKey() && WiFi.status() == WL_CONNECTED) {
        display.setTextColor(0x07E0);
        display.drawString("[GEMINI LIVE OK] ", 10, 64);
        String modelName = configManager.config().geminiModel;
        if (modelName.startsWith("models/")) modelName = modelName.substring(7);
        display.drawString((modelName + " (WS Pronto)").c_str(), 124, 64);
      } else {
        display.setTextColor(0xFDA0);
        display.drawString("[GEMINI LIVE --] ", 10, 64);
        display.setTextColor(TFT_WHITE);
        display.drawString("Sem Chave API ou Wi-Fi Desconectado", 124, 64);
      }

      // Panel 2: PaHub, TOF 90 (Ch 0), Unit ENV (Ch 1)
      display.fillRect(10, 88, 300, 26, 0x0841);
      if (unitPaHub.isFound()) {
        display.setTextColor(0x07E0);
        char hubStr[32];
        snprintf(hubStr, sizeof(hubStr), "[PaHub 0x%02X OK]", unitPaHub.getAddress());
        display.drawString(hubStr, 10, 101);
      } else {
        display.setTextColor(0xF800);
        display.drawString("[PaHub AUSENTE]", 10, 101);
      }

      if (unitPaHub.isTOFReady()) {
        display.setTextColor(0x07E0);
        char tofStr[40];
        if (distMm > 0) {
          snprintf(tofStr, sizeof(tofStr), "[TOF90 OK] %u cm (%u mm)", distMm / 10, distMm);
        } else {
          snprintf(tofStr, sizeof(tofStr), "[TOF90 OK] Dist: --");
        }
        display.drawString(tofStr, 134, 101);
      } else {
        display.setTextColor(0xF800);
        display.drawString("[TOF90 AUSENTE]", 134, 101);
      }

      display.fillRect(10, 116, 300, 24, 0x0841);
      if (unitPaHub.isENVReady()) {
        display.setTextColor(0x07E0);
        display.drawString("[ENV OK] ", 10, 128);
        display.setTextColor(0xFFE0); // Yellow
        char envStr[64];
        snprintf(envStr, sizeof(envStr), "%.1f C | %.0f%% UR | %.0f hPa", env.temperature, env.humidity, env.pressure);
        display.drawString(envStr, 70, 128);
      } else {
        display.setTextColor(0xF800);
        display.drawString("[ENV AUSENTE] Canal 1 sem resposta", 10, 128);
      }

      // Panel 3: Hardware CoreS3 & Battery
      display.fillRect(10, 154, 300, 20, 0x0841);
      display.setTextColor(M5.Imu.isEnabled() ? 0x07E0 : 0xF800);
      display.drawString(M5.Imu.isEnabled() ? "[IMU OK]" : "[IMU ERR]", 10, 164);
      display.setTextColor(SD.cardType() != CARD_NONE ? 0x07E0 : 0xF800);
      display.drawString(SD.cardType() != CARD_NONE ? "[SD OK]" : "[SD --]", 76, 164);
      display.setTextColor(0x07E0);
      display.drawString("[TOUCH OK]", 132, 164);

      uint16_t alsVal = 0;
      bool alsOk = readLtr553Als(alsVal);
      display.setTextColor(alsOk ? 0x07E0 : 0xFDA0);
      char alsStr[32];
      snprintf(alsStr, sizeof(alsStr), "[LTR %u lx]", alsVal);
      display.drawString(alsStr, 214, 164);

      display.fillRect(10, 174, 300, 20, 0x0841);
      float batVolts = M5StackChan.getBatteryVoltage();
      int batLevel = M5.Power.getBatteryLevel();
      uint8_t batPct = (batLevel >= 0) ? static_cast<uint8_t>(batLevel) :
                       (batVolts > 3.4f ? static_cast<uint8_t>(constrain((batVolts - 3.4f) / 0.8f * 100.0f, 0.0f, 100.0f)) : 0);
      display.setTextColor(batPct > 20 ? 0x07E0 : 0xFD40);
      char batStr[48];
      bool charging = (M5StackChan.getBatteryCurrent() < -0.01f) || M5.Power.isCharging();
      snprintf(batStr, sizeof(batStr), "Bateria: %u%% (%.2fV)%s", batPct, batVolts, charging ? " [CARREGANDO]" : "");
      display.drawString(batStr, 10, 184);
    }

    // Countdown and Progress
    display.setTextDatum(textdatum_t::middle_center);
    display.setTextSize(1);
    display.setTextColor(0xFFE0); // Yellow
    char cdStr[48];
    snprintf(cdStr, sizeof(cdStr), "Iniciando Pix em %u s... [Toque p/ pular]", static_cast<unsigned>(remainingSec));
    display.fillRect(20, 204, 280, 16, TFT_BLACK);
    display.drawString(cdStr, 160, 212);

    // Progress Bar
    display.drawRoundRect(8, 222, 304, 12, 4, 0x3DFE);
    int barW = (elapsed * 300) / durationMs;
    if (barW > 300) barW = 300;
    display.fillRoundRect(10, 224, barW, 8, 2, 0x07E0);

    delay(40);
  }

  // Clear before handing off to procedural face
  display.fillScreen(TFT_BLACK);
  Serial.println("BootDiag: diagnostic screen finished.");
}

void setup() {
  // CRITICAL HARDWARE SAFETY: Force Port B Vibrator DIN (GPIO 9) to LOW immediately on reset
  // so the motor transistor cannot conduct when the 5V rail powers on!
  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);
  pinMode(8, INPUT);

  Serial.begin(115200);
  esp_reset_reason_t rstReason = esp_reset_reason();
  Serial.printf("CoreS3 BOOT: reset_reason=%d\n", static_cast<int>(rstReason));
  M5StackChan.begin();
  M5.Power.setExtOutput(true); // Enable 5V boost for Port A, Port B and Grove accessories
  bool imu_ok = M5.Imu.begin(&M5.In_I2C, M5.getBoard());
  Serial.printf("IMU init: %s type=%d\n", imu_ok ? "ok" : "failed", static_cast<int>(M5.Imu.getType()));
  configureAudio();
  applySpeakerVolume();
  playBootDroidWhistle();
  emotion.begin();
  emotion.setUnitPaHub(&unitPaHub);
  emotion.setLiveFeeds(&liveFeeds);
  servoGestures.begin();
  camera.begin();
  M5StackChan.Motion.goHome();
  if (unitPaHub.begin()) {
    for (uint8_t channel = 0; channel < 8; ++channel) {
      unitPaHub.scanChannel(channel);
    }
  }
  initLtr553();
  liveFeeds.setGeminiLiveProbe(&gemini);
  liveFeeds.begin();
  // Safe defaults before SD config is loaded. ConfigManager may override after SD mount.
  ToolGatewayClient::Config gatewayConfig;
  gatewayConfig.enabled = false;
  gatewayConfig.baseUrl = "";
  gatewayConfig.robotId = "stackchan";
  toolGateway.begin(gatewayConfig);
  gemini.setToolBridge(&toolBridge);
  toolBridge.setGemini(&gemini);
  toolBridge.setMemoryStore(&memory);
  toolBridge.setUnitPaHub(&unitPaHub);
  toolBridge.setLiveFeeds(&liveFeeds);
  toolBridge.setGoogleCalendar(&googleCalendar);
  gemini.setEmotionController(&emotion);
  gemini.setMemoryStore(&memory);
  WebConfigServer::Config webConfigOptions;
  webConfigOptions.enabled = false;
  webConfigOptions.port = 80;
  webConfigOptions.robotId = "stackchan";
  webConfig.begin(webConfigOptions);
  drawStatus("BSP + config compile");

  // Compile-only: do not print or embed secrets. Tomorrow load key from SD/NVS.
  // Example SD mount path is preserved for compatibility investigation.
  bool sd_ok = SD.begin(GPIO_NUM_4, SPI, 25000000);
  Serial.printf("SD status: %s\n", sd_ok ? "ok" : "missing");
  if (sd_ok) {
    MemoryStore::Policy policy;
    policy.keepRawSessions = 3;
    policy.keepRawDays = 3;
    policy.maxEventFileBytes = 256 * 1024;
    policy.maxRetrievedChars = 3000;
    bool memory_ok = memory.begin(policy);
    Serial.printf("MemoryStore status: %s session=%s day=%s\n",
                  memory_ok ? "ok" : "failed",
                  memory.currentSessionId(),
                  memory.todayKey());
    if (memory_ok) {
      memory.compactIfNeeded();
    }
    bool cfg_ok = configManager.begin();
#if defined(LOCAL_SEED_CALENDAR_URL)
    if (!configManager.hasCalendarUrl()) {
      SD.mkdir("/app");
      SD.mkdir("/app/StackChan");
      SD.mkdir("/app/StackChan/secrets");
      File f = SD.open("/app/StackChan/secrets/calendar_url.txt", FILE_WRITE);
      if (f) {
        f.print(LOCAL_SEED_CALENDAR_URL);
        f.close();
        Serial.println("Google Calendar: seeded private URL to SD secrets successfully.");
      }
    }
#endif
    bool runtime_config_exists = SD.exists("/app/StackChan/config/runtime.json");
    Serial.printf("ConfigManager init: %s\n", cfg_ok ? "ok" : "failed");
    configManager.printRedactedStatus(Serial);
    g_speaker_volume = configManager.config().speakerVolume;
    configureAudio(8, 0);
    applySpeakerVolume();
    Serial.printf("Audio: speaker_volume=%u mic_magnification=8 mic_noise_filter=0 (Phase 3 operational)\n",
                  static_cast<unsigned>(g_speaker_volume));
    toolGateway.begin(configManager.gatewayConfig());
    bool wifi_ok = connectWifiFromConfig(configManager);
    if (wifi_ok) {
      // Sync clock via NTP for Brasilia time (UTC - 3h)
      configTime(-3 * 3600, 0, "a.st1.ntp.br", "pool.ntp.org", "time.nist.gov");
      Serial.println("NTP: time synchronization initiated for Brasilia (UTC-3).");
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 2000)) {
        Serial.printf("NTP: synchronized successfully (%04d-%02d-%02d %02d:%02d:%02d)\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      } else {
        Serial.println("NTP: sync timeout (proceeding with prefetch).");
      }
      liveFeeds.triggerBootPrefetch();
    }
    bool setup_ap_ok = false;
    if (!wifi_ok && shouldStartSetupAccessPoint(configManager, runtime_config_exists)) {
      setup_ap_ok = startSetupAccessPoint(configManager);
    }
    if (wifi_ok || setup_ap_ok) {
      if (wifi_ok) {
        webConfig.begin(configManager.webConfig());
      } else {
        WebConfigServer::Config setupWeb;
        setupWeb.enabled = true;
        setupWeb.port = 80;
        setupWeb.robotId = configManager.config().robotId.c_str();
        webConfig.begin(setupWeb);
      }
      // 10-second Boot Diagnostic Screen showing Wi-Fi, Gemini, PaHub 0x77, TOF90, ENV, and Battery
      showBootDiagnosticScreen(10000);

      // Ensure Core 0 boot prefetch completed (or wait up to 8s timeout) before releasing Gemini Live
      if (wifi_ok) {
        liveFeeds.waitForBootPrefetch(8000);
      }
      Serial.println("[BOOT_PREFETCH] Gemini Live released");

      const auto& cfg = configManager.config();
      if (wifi_ok && cfg.geminiEnabled) {
        if (configManager.hasGeminiApiKey()) {
          Serial.println("Gemini: configuring live session...");
          gemini.setModel(cfg.geminiModel);
          gemini.setVoiceName(cfg.geminiVoice);
          gemini.setVadConfig(cfg.vadPrefixPaddingMs, cfg.vadSilenceDurationMs,
                              false, // Phase 3 operational: START_SENSITIVITY_LOW
                              cfg.vadEndSensitivityLow,
                              cfg.vadTurnIncludesAllInput);
          gemini.setSystemPrompt(cfg.systemPrompt);
          gemini.setPersonaPrompt(cfg.personaPrompt);
          bool gemini_ok = gemini.begin(configManager.readGeminiApiKey().c_str());
          if (gemini_ok) {
            emotion.setEmotion("happy");
            g_emotion_override_until_ms = millis() + 3000;
            Serial.println("Boot: playing wake droid chirp...");
            playWakeDroidChirp();
            uint32_t chirpWait = 0;
            while (M5.Speaker.isPlaying() && chirpWait < 350) {
              delay(10);
              chirpWait += 10;
            }
            Serial.println("Boot: conversation starts DISARMED. Connecting WebSocket in background...");
            gemini.connect();
            Serial.println("Gemini: connected on boot (DISARMED) -> wave or touch to talk");
          } else {
            Serial.println("Gemini: init failed");
            emotion.setEmotion("neutral");
          }
        } else {
          Serial.println("Gemini: enabled but api key missing");
          emotion.setEmotion("neutral");
        }
      } else if (wifi_ok) {
        Serial.println("Gemini: disabled");
        emotion.setEmotion("neutral");
      } else {
        Serial.println("Gemini: skipped in setup access point mode");
        emotion.setEmotion("neutral");
      }
    } else {
      WebConfigServer::Config webOff;
      webOff.enabled = false;
      webOff.robotId = configManager.config().robotId.c_str();
      webConfig.begin(webOff);
      Serial.println("Gemini: skipped because WiFi is not connected");
      showBootDiagnosticScreen(10000);
      emotion.setEmotion("neutral");
    }
  }

  // Gemini API key is loaded by ConfigManager as a secret and must never be printed.
  g_last_human_activity_ms = millis();
}

void loop() {
  M5StackChan.update();

  // Dynamic Serial Commands (e.g. GAIN=16, GAIN=8, GAIN=4)
  while (Serial.available()) {
    String cmdLine = Serial.readStringUntil('\n');
    cmdLine.trim();
    if (cmdLine.startsWith("GAIN=")) {
      int newGain = cmdLine.substring(5).toInt();
      if (newGain >= 1 && newGain <= 24) {
        uint8_t oldGain = M5.Mic.config().magnification;
        uint32_t ts = millis();
        configureAudio(static_cast<uint8_t>(newGain), M5.Mic.config().noise_filter_level);
        gemini.notifyMicGainChanged(oldGain, static_cast<uint8_t>(newGain));
        Serial.printf("[CONFIG:MIC_GAIN] ts=%lu old_gain=%u new_gain=%u\n",
                      ts, static_cast<unsigned>(oldGain), static_cast<unsigned>(newGain));
      } else {
        Serial.printf("[CONFIG:MIC_GAIN] ERROR: invalid gain %d (valid: 1..24)\n", newGain);
      }
    }
  }

  uint32_t now = millis();
  if (M5.BtnPWR.isPressed()) {
    if (g_power_button_pressed_ms == 0) g_power_button_pressed_ms = now;
    if (now - g_power_button_pressed_ms >= kPowerButtonLedOffHoldMs) {
      blackoutExternalLightsForPowerOff();
    }
  } else {
    g_power_button_pressed_ms = 0;
  }

  // Physical Mute button (Alexa-style quick click on Power Button)
  if (M5.BtnPWR.wasClicked()) {
    g_physical_mute = !g_physical_mute;
    gemini.setPhysicalMute(g_physical_mute);
    if (g_physical_mute) {
      gemini.stopConversation();
      gemini.setDoNotDisturb(1440); // 24-hour mute
      emotion.setPhysicalMute(true);
      emotion.setDoNotDisturb(true);
      unitPaHub.buzzAlert();
      emotion.setEmotion("sleep");
      Serial.println("BtnPWR: MUDO FISICO ATIVADO (Alexa-style)");
    } else {
      g_physical_mute = false;
      gemini.clearDoNotDisturb();
      emotion.setPhysicalMute(false);
      emotion.setDoNotDisturb(false);
      unitPaHub.click();
      emotion.setEmotion("happy");
      g_emotion_override_until_ms = now + 2000;
      Serial.println("BtnPWR: MUDO FISICO DESATIVADO");
    }
  }

  if (g_power_button_leds_off) {
    blackoutExternalLightsForPowerOff();
    delay(50);
    return;
  }

  unitPaHub.tick();

  // Audio Priority Mode: Suspende WebConfig durante fala
  if (!gemini.isSpeaking()) {
    webConfig.loop();
  }

  gemini.loop();
  bool realAudioSpeaking = gemini.isSpeaking();
  emotion.setIsSpeaking(realAudioSpeaking);
  emotion.setMicActive(gemini.isRecording() && !realAudioSpeaking);
  emotion.setDoNotDisturb(gemini.isDoNotDisturb());
  emotion.setPhysicalMute(g_physical_mute);
  emotion.loop();

  // Update LiveFeedsManager system idle state for Core 0 background worker
  bool inSleep = (emotion.currentEmotion() == "sleep");
  bool systemIdle = (WiFi.status() == WL_CONNECTED) &&
                    ((inSleep && !realAudioSpeaking && !gemini.isRecording() && !gemini.isTurnBusy()) ||
                     (gemini.turnState() == GeminiLiveProbe::LiveTurnState::IDLE && !realAudioSpeaking && !gemini.isRecording() && !gemini.isTurnBusy()));
  liveFeeds.setSystemIdle(systemIdle);
  liveFeeds.loop();

  // Servos: mantem servoGestures.loop() ativo apenas se houver gesto ja em curso, sem novos gestos
  servoGestures.loop();

  // Reset temporary emotional reactions back to neutral after duration
  if (g_emotion_override_until_ms != 0 && now >= g_emotion_override_until_ms) {
    g_emotion_override_until_ms = 0;
    if (!gemini.isSpeaking() && !gemini.isRecording()) {
      emotion.setEmotion("neutral");
    }
  }

  // Physical motion sensing: IMU suspenso durante fala para economizar barramento I2C e CPU
  if (!gemini.isSpeaking() && M5.Imu.isEnabled()) {
    M5.Imu.update();
    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
      M5.Imu.getGyro(&gx, &gy, &gz);
      float gyroMag = sqrtf(gx * gx + gy * gy + gz * gz);
      float totalG = sqrtf(ax * ax + ay * ay + az * az);
      static float lastTotalG = 1.0f;
      float jerk = fabsf(totalG - lastTotalG);
      lastTotalG = totalG;

      static uint32_t lastLiftMs = 0;
      static bool isCurrentlyLifted = false;
      static uint32_t tableStableSinceMs = 0;
      static uint32_t lastImuLogMs = 0;
      static uint32_t lastServoActiveMs = 0;

      if (servoGestures.active()) {
        lastServoActiveMs = now;
      }
      bool servoMoving = servoGestures.active() || (now - lastServoActiveMs < 850);

      // Periodic IMU logging (once per second)
      if (now - lastImuLogMs >= 1000) {
        lastImuLogMs = now;
        Serial.printf("IMU: ax=%.2f ay=%.2f az=%.2f totalG=%.2f gyro=%.1f jerk=%.2f lifted=%d servoMoving=%d\n",
                      ax, ay, az, totalG, gyroMag, jerk, isCurrentlyLifted ? 1 : 0, servoMoving ? 1 : 0);
      }

      // Calculate dynamic acceleration and filtered gravity for Eilik-style motion state machine
      float dynamicAccel = fabsf(totalG - 1.0f);
      static float s_gravX = 0.0f, s_gravY = 0.0f, s_gravZ = 1.0f;
      s_gravX = s_gravX * 0.8f + ax * 0.2f;
      s_gravY = s_gravY * 0.8f + ay * 0.2f;
      s_gravZ = s_gravZ * 0.8f + az * 0.2f;

      bool isCalmOnTable = (dynamicAccel < 0.10f && gyroMag < 18.0f);
      // Lift: gentle sustained displacement, suspended during motor vibration and servo movement
      bool candidateLift = !unitPaHub.isVibrating() && !servoMoving &&
                           (dynamicAccel > 0.15f || jerk > 0.16f) &&
                           (gyroMag > 10.0f && gyroMag < 120.0f);

      // Log when physical lift conditions are met but rejected solely due to servo motion
      static bool s_servo_blocked_latched = false;
      if (servoMoving && !unitPaHub.isVibrating() &&
          (dynamicAccel > 0.15f || jerk > 0.16f) &&
          (gyroMag > 10.0f && gyroMag < 120.0f)) {
        if (!s_servo_blocked_latched) {
          s_servo_blocked_latched = true;
          Serial.println("[DIAG:LIFT] candidate blocked by servo");
        }
      } else if (!servoMoving) {
        s_servo_blocked_latched = false;
      }

      enum class EilikMotionState : uint8_t {
        RESTING,
        MAYBE_LIFTING,
        LIFTED
      };
      static EilikMotionState s_motionState = EilikMotionState::RESTING;
      static uint32_t s_motionStateTimeMs = 0;

      switch (s_motionState) {
        case EilikMotionState::RESTING:
          isCurrentlyLifted = false;
          if (candidateLift && (now - lastLiftMs > 2000)) {
            s_motionState = EilikMotionState::MAYBE_LIFTING;
            s_motionStateTimeMs = now;
            Serial.printf("[DIAG:LIFT] RESTING -> MAYBE_LIFTING t=%lu totalG=%.2f dynA=%.2f gyro=%.1f jerk=%.2f axes=(%.2f,%.2f,%.2f) servo=%d vib=%d gesture=%s spk=%d\n",
                          now, totalG, dynamicAccel, gyroMag, jerk, ax, ay, az,
                          servoMoving ? 1 : 0, unitPaHub.isVibrating() ? 1 : 0,
                          servoGestures.active() ? servoGestures.currentGesture().c_str() : "none",
                          gemini.isSpeaking() ? 1 : 0);
          }
          break;

        case EilikMotionState::MAYBE_LIFTING:
          if (servoMoving) {
            s_motionState = EilikMotionState::RESTING;
            s_motionStateTimeMs = 0;
            Serial.println("[DIAG:LIFT] MAYBE_LIFTING aborted by servo");
          } else if (unitPaHub.isVibrating()) {
            s_motionState = EilikMotionState::RESTING;
            s_motionStateTimeMs = 0;
            Serial.println("[DIAG:LIFT] MAYBE_LIFTING aborted by vibration");
          } else if (isCalmOnTable) {
            uint32_t accumMs = now - s_motionStateTimeMs;
            s_motionState = EilikMotionState::RESTING;
            s_motionStateTimeMs = 0;
            Serial.printf("[DIAG:LIFT] MAYBE_LIFTING -> RESTING (calm/bump rejected) t=%lu accum=%lums totalG=%.2f dynA=%.2f gyro=%.1f jerk=%.2f axes=(%.2f,%.2f,%.2f) servo=%d vib=%d gesture=%s spk=%d\n",
                          now, accumMs, totalG, dynamicAccel, gyroMag, jerk, ax, ay, az,
                          servoMoving ? 1 : 0, unitPaHub.isVibrating() ? 1 : 0,
                          servoGestures.active() ? servoGestures.currentGesture().c_str() : "none",
                          gemini.isSpeaking() ? 1 : 0);
          } else if (now - s_motionStateTimeMs >= 250) {
            // Deslocamento suave e sustentado confirmado: robo foi levantada da mesa!
            uint32_t accumMs = now - s_motionStateTimeMs;
            s_motionState = EilikMotionState::LIFTED;
            s_motionStateTimeMs = now;
            lastLiftMs = now;
            isCurrentlyLifted = true;
            g_last_human_activity_ms = now;
            emotion.setLifted(true);
            emotion.setEmotion("scared"); // Expressao de susto com altura
            g_emotion_override_until_ms = now + 4000;
            unitPaHub.vibrateLift();
            servoGestures.queueGesture("curious_tilt");
            playLiftSurprisedWhistle();
            static uint32_t s_last_lift_speech_ms = 0;
            if (now - s_last_lift_speech_ms > 8000) {
              s_last_lift_speech_ms = now;
              speakFixedReaction("LEVANTADA DA MESA", kLiftPhrases, sizeof(kLiftPhrases) / sizeof(kLiftPhrases[0]));
            }
            Serial.printf("[DIAG:LIFT] MAYBE_LIFTING -> LIFTED (confirmed lifted!) t=%lu accum=%lums totalG=%.2f dynA=%.2f gyro=%.1f jerk=%.2f axes=(%.2f,%.2f,%.2f) servo=%d vib=%d gesture=%s spk=%d cond=(dur>=250 && !calm)\n",
                          now, accumMs, totalG, dynamicAccel, gyroMag, jerk, ax, ay, az,
                          servoMoving ? 1 : 0, unitPaHub.isVibrating() ? 1 : 0,
                          servoGestures.active() ? servoGestures.currentGesture().c_str() : "none",
                          gemini.isSpeaking() ? 1 : 0);
          }
          break;

        case EilikMotionState::LIFTED: {
          isCurrentlyLifted = true;
          // RETORNO A MESA: superficie calma sustentada por 800ms
          if (isCalmOnTable) {
            if (tableStableSinceMs == 0) tableStableSinceMs = now;
            else if (now - tableStableSinceMs >= 800) {
              s_motionState = EilikMotionState::RESTING;
              tableStableSinceMs = 0;
              isCurrentlyLifted = false;
              emotion.setLifted(false);
              emotion.setEmotion("happy"); // Alivio ao retornar a seguranca da mesa
              g_emotion_override_until_ms = now + 3500;
              unitPaHub.vibrateRelief();
              servoGestures.queueGesture("nod_yes");
              static uint32_t s_last_relief_speech_ms = 0;
              if (now - s_last_relief_speech_ms > 8000) {
                s_last_relief_speech_ms = now;
                speakFixedReaction("DE VOLTA NA MESA", kTableReliefPhrases, sizeof(kTableReliefPhrases) / sizeof(kTableReliefPhrases[0]));
              }
              Serial.printf("[DIAG:LIFT] LIFTED -> RESTING (settled back down on table) t=%lu totalG=%.2f dynA=%.2f gyro=%.1f axes=(%.2f,%.2f,%.2f)\n",
                            now, totalG, dynamicAccel, gyroMag, ax, ay, az);
            }
          } else {
            tableStableSinceMs = 0;
          }
          break;
        }
      }
    }
  }

  // =========================================================================
  // ToF90 SENSORY SAMPLING & EVENT DISPATCH:
  // Intervalo dinâmico: 50ms quando livre para resposta rápida a gestos;
  // 350ms durante a fala (Audio Priority Mode) apenas para manter estado de presença.
  // =========================================================================
  uint32_t tofInterval = gemini.isSpeaking() ? 350 : 50;
  if (unitPaHub.isFound() && now - g_last_tof_fast_ms >= tofInterval) {
    g_last_tof_fast_ms = now;
    uint16_t dist = unitPaHub.readDistanceTof90Mm();
    if (dist > 0) g_current_distance_mm = dist;

    // Proximidade extrema (<80 mm) com debounce estrito de 3 leituras consecutivas:
    // - Exige 3 leituras consecutivas válidas
    // - Leitura 0, inválida ou outlier (>1500 mm) -> reset contador
    // - Leitura >= 80 mm -> reset contador
    // - Janela máxima para completar as 3 leituras: 300 ms
    static uint8_t s_tof_near_count = 0;
    static uint32_t s_tof_first_near_ms = 0;
    static float s_tof_filtered_mm = 0.0f;
    static uint32_t s_last_scared_ms = 0;

    uint16_t raw_mm = dist;
    bool is_outlier_or_invalid = (raw_mm == 0 || raw_mm > 1500);

    if (is_outlier_or_invalid || raw_mm >= 80) {
      if (s_tof_near_count > 0) {
        uint32_t elapsed_ms = (s_tof_first_near_ms > 0) ? (now - s_tof_first_near_ms) : 0;
        Serial.printf("[DIAG:TOF_NEAR] raw_mm=%u filtered_mm=%.1f near_count=%u first_near_ms=%lu elapsed_ms=%lu trigger=rejected (reset: %s)\n",
                      raw_mm, s_tof_filtered_mm, s_tof_near_count, s_tof_first_near_ms, elapsed_ms,
                      is_outlier_or_invalid ? "invalid/outlier" : "dist>=80mm");
      }
      s_tof_near_count = 0;
      s_tof_first_near_ms = 0;
      if (raw_mm > 0 && raw_mm <= 1500) s_tof_filtered_mm = static_cast<float>(raw_mm);
    } else {
      // raw_mm > 0 && raw_mm < 80 mm
      uint32_t elapsed_ms = 0;
      if (s_tof_near_count == 0) {
        s_tof_near_count = 1;
        s_tof_first_near_ms = now;
        s_tof_filtered_mm = static_cast<float>(raw_mm);
        elapsed_ms = 0;
        Serial.printf("[DIAG:TOF_NEAR] raw_mm=%u filtered_mm=%.1f near_count=1 first_near_ms=%lu elapsed_ms=0 trigger=pending\n",
                      raw_mm, s_tof_filtered_mm, s_tof_first_near_ms);
      } else {
        elapsed_ms = now - s_tof_first_near_ms;
        if (elapsed_ms > 300) {
          Serial.printf("[DIAG:TOF_NEAR] raw_mm=%u filtered_mm=%.1f near_count=%u first_near_ms=%lu elapsed_ms=%lu trigger=rejected (window>300ms)\n",
                        raw_mm, s_tof_filtered_mm, s_tof_near_count, s_tof_first_near_ms, elapsed_ms);
          s_tof_near_count = 1;
          s_tof_first_near_ms = now;
          s_tof_filtered_mm = static_cast<float>(raw_mm);
        } else {
          s_tof_near_count++;
          s_tof_filtered_mm = (s_tof_filtered_mm * 0.5f) + (raw_mm * 0.5f);
        }
      }

      if (s_tof_near_count >= 3) {
        elapsed_ms = now - s_tof_first_near_ms;
        if (!gemini.isSpeaking() && !gemini.isRecording() && !gemini.isDoNotDisturb()) {
          if (now - s_last_scared_ms > 7000) {
            s_last_scared_ms = now;
            g_last_human_activity_ms = now;

            Serial.printf("[DIAG:TOF_NEAR] raw_mm=%u filtered_mm=%.1f near_count=%u first_near_ms=%lu elapsed_ms=%lu trigger=accepted\n",
                          raw_mm, s_tof_filtered_mm, s_tof_near_count, s_tof_first_near_ms, elapsed_ms);
            Serial.printf("Presence: ESPANTO! Objeto/rosto muito proximo (raw=%u filtered=%.1f mm)\n",
                          raw_mm, s_tof_filtered_mm);
            emotion.setEmotion("scared");
            g_emotion_override_until_ms = now + 3500;
            unitPaHub.buzzAlert();
            servoGestures.queueGesture("curious_tilt");
            playLiftSurprisedWhistle();

            static uint32_t s_last_close_speech_ms = 0;
            if (now - s_last_close_speech_ms > 9000) {
              s_last_close_speech_ms = now;
              speakFixedReaction("MUITO PROXIMO", kClosePhrases, sizeof(kClosePhrases) / sizeof(kClosePhrases[0]));
            }
          } else {
            Serial.printf("[DIAG:TOF_NEAR] raw_mm=%u filtered_mm=%.1f near_count=%u first_near_ms=%lu elapsed_ms=%lu trigger=rejected (cooldown)\n",
                          raw_mm, s_tof_filtered_mm, s_tof_near_count, s_tof_first_near_ms, elapsed_ms);
          }
        } else {
          Serial.printf("[DIAG:TOF_NEAR] raw_mm=%u filtered_mm=%.1f near_count=%u first_near_ms=%lu elapsed_ms=%lu trigger=rejected (busy)\n",
                        raw_mm, s_tof_filtered_mm, s_tof_near_count, s_tof_first_near_ms, elapsed_ms);
        }
        s_tof_near_count = 0;
        s_tof_first_near_ms = 0;
      }
    }

    TofEvent tofEvt = g_tofDetector.update(dist, now);

    // Durante fala, suspende temporariamente gesto rápido de mão
    if (gemini.isSpeaking()) {
      tofEvt = (tofEvt == TofEvent::HAND_WAVE) ? TofEvent::NONE : tofEvt;
    }

    if (tofEvt == TofEvent::HAND_WAVE) {
      Serial.printf("TofEvent: HAND_WAVE detectado (dist=%u mm)! Avaliando prioridades...\n", dist);

      // Hierarquia Geral de Decisão Central:
      // Prioridade 1: MUDO FÍSICO
      if (g_physical_mute) {
        Serial.println("Action: HAND_WAVE bloqueado por MUDO FISICO");
        unitPaHub.click();
      }
      // Prioridade 2: DND (Não Perturbe)
      else if (gemini.isDoNotDisturb()) {
        Serial.println("Action: HAND_WAVE bloqueado por DND");
        unitPaHub.click();
      }
      // Prioridade 3: Robô Falando (isSpeaking)
      else if (gemini.isSpeaking()) {
        Serial.println("Action: Robô falando, ignorando aceno para não cortar fala");
      }
      // Prioridade 4: Conversa Ativa / Microfone já gravando (isRecording)
      else if (gemini.isRecording()) {
        Serial.println("Action: Conversa já ativa -> feedback visual (smile + nod)");
        g_last_human_activity_ms = now;
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = now + 2000;
        unitPaHub.click();
        servoGestures.queueGesture("nod_yes");
      }
      // Prioridade 5: Gesto válido para abrir conversa!
      else {
        Serial.println("Action: Gesto aceito -> Acordando e abrindo microfone!");
        g_last_human_activity_ms = now;
        playWakeDroidChirp();
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = now + 2500;
        unitPaHub.click();
        servoGestures.queueGesture("nod_yes");

        // Pequeno atraso para o chime tocar antes de capturar voz no mic
        uint32_t chimeWait = 0;
        while (M5.Speaker.isPlaying() && chimeWait < 250) {
          delay(10);
          chimeWait += 10;
        }
        gemini.armConversation("FAST_GESTURE");
      }
    }
    else if (tofEvt == TofEvent::USER_PRESENT) {
      Serial.printf("TofEvent: USER_PRESENT (usuário chegou à mesa a %u mm)\n", dist);
      g_last_human_activity_ms = now;
      // Presença estável NUNCA abre o microfone!
      // Apenas atualiza atividade e faz acolhimento visual se estava dormindo
      if (emotion.currentEmotion() == "sleep" && !g_physical_mute && !gemini.isDoNotDisturb()) {
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = now + 3500;
        servoGestures.queueGesture("look_at_user");
        unitPaHub.click();
        playWakeDroidChirp();
      }
    }
    else if (tofEvt == TofEvent::USER_LEFT) {
      Serial.println("TofEvent: USER_LEFT (ausência contínua >15s confirmada)");
      // Mesa vazia: kIdleSleepMs (120s) cuidará de colocar o robô para dormir suavemente
    }
  }

  // Presença contínua à mesa: mantém o robô acordado enquanto o usuário estiver trabalhando
  if (g_tofDetector.isUserPresent()) {
    g_last_human_activity_ms = now;
  }

  // ENV III: Telemetria ambiental basica em baixa frequencia (3000ms).
  // Totalmente desvinculado de gestos, sopro, fala, presenca ou emocoes.
  if (unitPaHub.isFound() && !gemini.isSpeaking() && now - g_last_sensor_read_ms >= 3000) {
    g_last_sensor_read_ms = now;
    uint16_t distanceMm = 0;
    UnitPaHubManager::EnvData env;
    if (unitPaHub.readSensors(distanceMm, env)) {
      if (distanceMm == 0 && g_current_distance_mm > 0) distanceMm = g_current_distance_mm;
      if (env.valid) {
        g_latest_temp = env.temperature;
        g_latest_hum = env.humidity;
        g_latest_pressure = env.pressure;
      }
      Serial.printf("Units: tof_mm=%u env=%s temp_c=%.2f humidity=%.2f pressure_hpa=%.2f\n",
                    static_cast<unsigned>(distanceMm), env.valid ? "ok" : "missing",
                    env.temperature, env.humidity, env.pressure);

      const uint32_t sensorNow = millis();

      // Conexão proativa com Gemini Live quando o usuário estiver presente à mesa
      static uint32_t s_last_proactive_connect_ms = 0;
      if (g_tofDetector.isUserPresent() && !gemini.isReady() && !gemini.isDoNotDisturb() &&
          WiFi.status() == WL_CONNECTED && sensorNow - s_last_proactive_connect_ms > 8000) {
        s_last_proactive_connect_ms = sensorNow;
        Serial.println("Presence: usuário na mesa e Gemini desconectado -> reconectando proativamente");
        gemini.connect();
      }
    } else {
      Serial.println("Units: read failed");
    }
  }

  // Ideia E: Day/Night cycle via LTR-553ALS ambient light sensor
  if (now - g_last_als_read_ms >= 2000) {
    g_last_als_read_ms = now;
    uint16_t alsAvg = 0;
    if (readLtr553Als(alsAvg)) {
      if (alsAvg < 6) {
        if (g_darkness_start_ms == 0) {
          g_darkness_start_ms = now;
        } else if (now - g_darkness_start_ms > 15000 && !g_night_mode) {
          g_night_mode = true;
          Serial.printf("LTR-553: Darkness confirmed (%u lux avg) -> night sleep mode\n", alsAvg);
          emotion.setEmotion("sleep");
          M5StackChan.showRgbColor(0, 0, 0);
          M5.Display.setBrightness(0); // Blank LCD to keep bedroom dark
        }
      } else {
        g_darkness_start_ms = 0;
        if (g_night_mode && alsAvg > 35) {
          g_night_mode = false;
          Serial.printf("LTR-553: Morning/room light restored (%u lux avg) -> wake up!\n", alsAvg);
          M5.Display.setBrightness(127);
          emotion.setEmotion("happy");
          g_emotion_override_until_ms = now + 4000;
          playWakeDroidChirp();
          unitPaHub.buzzAlert();
          servoGestures.queueGesture("look_at_user");
          if (!g_physical_mute && !gemini.isDoNotDisturb()) {
            uint32_t chirpWait = 0;
            while (M5.Speaker.isPlaying() && chirpWait < 350) {
              delay(10);
              chirpWait += 10;
            }
            gemini.armConversation("TOUCH");
          } else {
            Serial.println("LTR-553: Morning wakeup conversation skipped (MUDO FISICO / DND active)");
          }
        }
      }
    }
  }

  // Touch Sensor Si12T handling: Isolate Middle/Petting from dialogue toggle
  auto touchIntensities = M5StackChan.TouchSensor.getIntensities();
  uint8_t frontTouch = touchIntensities[0];
  uint8_t midTouch = touchIntensities[1];
  uint8_t backTouch = touchIntensities[2];
  bool swiped = M5StackChan.TouchSensor.wasSwiped();

  static uint32_t s_mid_touch_start_ms = 0;
  static bool s_is_prolonged_petting = false;

  // Affection / Petting on middle electrode: NEVER toggles or cuts off dialogue!
  if (midTouch > 0 || swiped) {
    g_last_human_activity_ms = now;
    if (s_mid_touch_start_ms == 0) s_mid_touch_start_ms = now;

    // Ideia D: Touching > 1.8s activates continuous gentle Cat Purring mode
    if (now - s_mid_touch_start_ms >= 1800) {
      if (!s_is_prolonged_petting) {
        s_is_prolonged_petting = true;
        emotion.setProlongedPetting(true);
        emotion.setEmotion("meigo");
        g_emotion_override_until_ms = now + 4000;
        unitPaHub.catPurr();
        servoGestures.queueGesture("curious_tilt");
        playCatPurrDocileSound();
        Serial.println("Touch: PROLONGED PETTING (Cat purr mode active)!");
      }
    }
  } else {
    if (s_is_prolonged_petting) {
      s_is_prolonged_petting = false;
      emotion.setProlongedPetting(false);
      unitPaHub.stopHaptic();
      s_mid_touch_start_ms = 0;
      Serial.println("Touch: prolonged petting released");
    } else if (s_mid_touch_start_ms != 0) {
      // Short affectionate pet / tap on middle
      uint32_t petDuration = now - s_mid_touch_start_ms;
      s_mid_touch_start_ms = 0;
      if (petDuration < 1800) {
        Serial.printf("Touch: middle affectionate pet (duration=%u ms)\n", petDuration);
        unitPaHub.purr();
        emotion.setEmotion("meigo");
        g_emotion_override_until_ms = now + 3500;
        servoGestures.queueGesture("curious_tilt");
        playCatPurrDocileSound();
        if (emotion.currentEmotion() == "sleep" && !g_physical_mute && !gemini.isDoNotDisturb()) {
          playWakeDroidChirp();
        }
      }
    }
  }

  // Body touch on front/back: physical interaction, wakes robot if asleep, NEVER turns off dialogue!
  if ((frontTouch > 0 || backTouch > 0) && midTouch == 0 && !swiped) {
    g_last_human_activity_ms = now;
    static uint32_t s_last_body_touch_ms = 0;
    if (now - s_last_body_touch_ms > 800) {
      s_last_body_touch_ms = now;
      unitPaHub.click();
      if (g_physical_mute) {
        Serial.println("Touch: front/back touch during MUDO FISICO -> tactile feedback only, conversation blocked");
      } else if (emotion.currentEmotion() == "sleep") {
        if (!gemini.isDoNotDisturb()) {
          emotion.setEmotion("happy");
          g_emotion_override_until_ms = now + 3000;
          playWakeDroidChirp();
          unitPaHub.buzzAlert();
          servoGestures.queueGesture("nod");
          gemini.armConversation("TOUCH");
        } else {
          Serial.println("Touch: front/back touch blocked by DND");
        }
      }
    }
  }

  // Dialogue toggle: triggered ONLY by intentional screen tap or HTTP request
  bool dialogueToggle = false;

  if (g_voice_toggle_requested) {
    g_voice_toggle_requested = false;
    Serial.println("HTTP voice toggle requested");
    dialogueToggle = true;
  }

#if defined(ARDUINO_M5STACK_CORES3)
  static bool s_screen_touching = false;
  static bool s_drawer_triggered = false;
  static int16_t s_touch_start_x = 0;
  static int16_t s_touch_start_y = 0;
  static uint32_t s_touch_start_ms = 0;

  auto t = M5.Touch.getDetail();
  if (t.wasPressed() || (t.isPressed() && !s_screen_touching)) {
    s_screen_touching = true;
    s_touch_start_x = t.x;
    s_touch_start_y = t.y;
    s_touch_start_ms = now;
    s_drawer_triggered = false;
  }

  // Dynamic swipe down detection while finger is sliding down
  if (s_screen_touching && !s_drawer_triggered) {
    int16_t dy = t.y - s_touch_start_y;
    int16_t dx = abs(t.x - s_touch_start_x);
    if (s_touch_start_y < 175 && dy >= 20 && dy > (dx / 2)) {
      s_drawer_triggered = true;
      Serial.printf("Display: SWIPE DOWN drawer activated (start_y=%d cur_y=%d dy=%d dx=%d)\n", s_touch_start_y, t.y, dy, dx);
      unitPaHub.click();
      int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
      String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
      float batVolts = M5StackChan.getBatteryVoltage();
      int batLevel = M5.Power.getBatteryLevel();
      uint8_t batPct = (batLevel >= 0) ? static_cast<uint8_t>(batLevel) :
                       (batVolts > 3.4f ? static_cast<uint8_t>(constrain((batVolts - 3.4f) / 0.8f * 100.0f, 0.0f, 100.0f)) : 0);
      bool charging = (M5StackChan.getBatteryCurrent() < -0.01f) || M5.Power.isCharging();
      bool geminiReady = gemini.isReady();
      emotion.setQuickStatusData(rssi, ip, batPct, batVolts, charging, geminiReady, g_latest_temp, g_latest_hum);
      emotion.showQuickStatus(6000);
    }
  }

  if ((t.wasReleased() || (!t.isPressed() && s_screen_touching)) && s_screen_touching) {
    s_screen_touching = false;
    if (!s_drawer_triggered) {
      if (emotion.isQuickStatusActive()) {
        emotion.hideQuickStatus();
        Serial.println("Display: quick status drawer dismissed by touch");
      } else {
        // Normal screen tap
        Serial.printf("Display touch click at x=%d y=%d\n", t.x, t.y);
        dialogueToggle = true;
      }
    }
    s_drawer_triggered = false;
  }
#endif

  if (dialogueToggle) {
    g_last_human_activity_ms = millis();
    unitPaHub.click(); // Tactile click feedback on touch
    memory.appendEvent("user", "touch pressed", "touch");

    if (g_physical_mute) {
      Serial.println("DialogueToggle: touch received during MUDO FISICO -> click feedback only; DND and conversation locked");
    } else {
      if (gemini.isDoNotDisturb()) {
        gemini.clearDoNotDisturb();
        emotion.setDoNotDisturb(false);
        Serial.println("DND: cancelled by user touch");
      }

      // If sleeping, wake up immediately
      if (emotion.currentEmotion() == "sleep") {
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = millis() + 3000;
        playWakeDroidChirp();
        unitPaHub.buzzAlert();
        servoGestures.queueGesture("nod");
      }

      if (emotion.currentEmotion() == "sleep" || !gemini.isConversationArmed() || !gemini.isRecording()) {
        playWakeDroidChirp();
        uint32_t chirpWait = 0;
        while (M5.Speaker.isPlaying() && chirpWait < 350) {
          delay(10);
          chirpWait += 10;
        }
        bool queued = gemini.armConversation("TOUCH");
        if (queued) {
          emotion.setEmotion("thinking");
          drawStatus(gemini.isReady() ? "dialog: preparing mic..." : "Gemini connecting...");
        } else {
          emotion.setEmotion("error");
          drawStatus("Gemini not configured");
        }
      } else {
        // Touch when already actively recording: smile, haptic click, and nod without disconnecting
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = millis() + 2000;
        unitPaHub.click();
        servoGestures.queueGesture("nod_yes");
      }
    }
  }

  now = millis();
  // Keep human activity timestamp refreshed during active conversation, recording, or speaking
  if (gemini.isRecording() || gemini.isSpeaking() || gemini.continuousConversation()) {
    g_last_human_activity_ms = now;
  }

  // Speaking micro-movement: temporariamente suspenso para teste de Audio Priority Mode
  // (evita contencao de tarefas/mutex com motion_task e picos de corrente nos servos durante a fala)
  // static uint32_t s_last_speaking_motion_ms = 0;
  // if (gemini.isSpeaking() && !servoGestures.active() && now - s_last_speaking_motion_ms > 1800) {
  //   s_last_speaking_motion_ms = now;
  //   servoGestures.queueGesture("speaking_micro_motion");
  // }

  // Idle micro-movement / breathing simulation: small subtle adjustment every ~7.5 seconds while resting
  if (emotion.currentEmotion() != "sleep" && !servoGestures.active() &&
      !gemini.isRecording() && !gemini.isSpeaking() && g_emotion_override_until_ms == 0 &&
      now - g_last_idle_breath_ms > 7500) {
    g_last_idle_breath_ms = now;
    servoGestures.queueGesture("idle_breath");
  }

  if (!gemini.isRecording() && !gemini.isSpeaking() && !gemini.continuousConversation() &&
      emotion.currentEmotion() != "sleep" &&
      g_emotion_override_until_ms == 0 &&
      g_last_human_activity_ms &&
      now - g_last_human_activity_ms > kIdleSleepMs) {
    Serial.println("Status: idle timeout -> sleep");
    emotion.setEmotion("sleep");
    servoGestures.queueGesture("center_head");
  }

  // Durante a fala, roda livre sem delay(2) para maxima frequencia de atendimento ao speaker DMA
  if (!gemini.isSpeaking()) {
    delay(2);
  }
}
