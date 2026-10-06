#include <Arduino.h>
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#include <M5Unified.h>
#include <M5StackChan.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_sntp.h>
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
#include "PixBehaviorManager.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#endif

GeminiLiveProbe gemini;
MemoryStore memory(SD);
TofGestureDetector g_tofDetector;
PixBehaviorManager g_behaviorManager;
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
static bool g_is_currently_lifted = false;
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
static bool g_pending_boot_pet = false;

uint16_t getCachedTofDistanceMm() { return g_current_distance_mm; }
bool getCachedTofPresence() { return g_tof_presence; }
float getCachedTemp() { return g_latest_temp; }
float getCachedHumidity() { return g_latest_hum; }
float getCachedPressure() { return g_latest_pressure; }

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

struct PendingSensoryReaction {
  bool active = false;
  const char* eventName = nullptr;
  String turnText;
  uint32_t queuedMs = 0;
};
static PendingSensoryReaction s_pending_reaction;

static void speakFixedReaction(const char* eventName, const char* const phrases[], size_t count) {
  if (gemini.isDoNotDisturb()) {
    s_pending_reaction.active = false;
    return;
  }

  size_t idx = random(0, count);
  String turn = "[SISTEMA SENSORIAL - ";
  turn += eventName;
  turn += "]: Diga exatamente esta frase em voz alta com muita expressividade e emoção: \"";
  turn += phrases[idx];
  turn += "\"";

  if (gemini.isTurnBusy()) {
    s_pending_reaction.active = true;
    s_pending_reaction.eventName = eventName;
    s_pending_reaction.turnText = turn;
    s_pending_reaction.queuedMs = millis();
    Serial.printf("SensorySpeech [%s]: deferred (turn busy: state=%s speaking=%d debounce=%d) -> queued: %s\n",
                  eventName ? eventName : "SENSORY",
                  gemini.turnStateString(),
                  gemini.isSpeaking() ? 1 : 0,
                  gemini.isInPostSpeechDebounce() ? 1 : 0,
                  phrases[idx]);
    return;
  }

  s_pending_reaction.active = false;
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

static bool startWifiAsync(const ConfigManager& cfg) {
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
  Serial.printf("WiFi: initiating station connection (async) ssid=%s\n", c.wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  // Keep DHCP addressing, but pin reliable resolvers for outbound Gemini/gateway connections.
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE,
              IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
  WiFi.begin(c.wifiSsid.c_str(), password.c_str());
  return true;
}

static void ntpTimeSyncNotificationCallback(struct timeval* tv) {
  time_t nowSec = tv->tv_sec;
  struct tm utcTm {};
  gmtime_r(&nowSec, &utcTm);
  if (M5.Rtc.isEnabled() && (utcTm.tm_year + 1900 >= 2026)) {
    M5.Rtc.setDateTime(&utcTm);
    Serial.printf("Time: RTC updated from NTP (UTC: %04d-%02d-%02d %02d:%02d:%02d)\n",
                  utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
                  utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec);
  }
  emotion.requestFaceRedraw();
}

static void configureNtpOnce() {
  static bool s_ntp_init = false;
  if (!s_ntp_init && WiFi.status() == WL_CONNECTED) {
    s_ntp_init = true;
    sntp_set_time_sync_notification_cb(ntpTimeSyncNotificationCallback);
    configTime(0, 0, "a.st1.ntp.br", "pool.ntp.org", "time.nist.gov");
    ConfigManager::applyTimezone(configManager.config().timezoneOffset);
    Serial.printf("Time: NTP configured in UTC and timezone reapplied (offset=%d)\n",
                  configManager.config().timezoneOffset);
  }
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

static void showBootDashboardScreen(uint32_t durationMs = 15000, bool geminiConfigured = false) {
  Serial.printf("BootDashboard: displaying %u ms dashboard screen (touch screen to skip)...\n", static_cast<unsigned>(durationMs));
  auto& display = M5StackChan.Display();
  display.fillScreen(TFT_BLACK);

  // 1. Header (Y=0..34)
  display.setTextDatum(textdatum_t::middle_center);
  display.setTextSize(2);
  display.setTextColor(0x3DFE); // Electric Cyan
  display.drawString("PIX", 160, 14);
  display.setTextSize(1);
  display.setTextColor(0x9DF8); // Soft Lavender
  display.drawString("Inicializando sistema...", 160, 28);

  // 2. Static Cards Frames
  // Row 1 (Y=36..96, H=60, r=8): Wi-Fi (12, 142) and IA (166, 142)
  display.fillRoundRect(12, 36, 142, 60, 8, 0x0841);
  display.drawRoundRect(12, 36, 142, 60, 8, 0x1948);
  display.setTextDatum(textdatum_t::top_left);
  display.setTextSize(1);
  display.setTextColor(0x3DFE);
  display.drawString("WIFI", 22, 42);

  display.fillRoundRect(166, 36, 142, 60, 8, 0x0841);
  display.drawRoundRect(166, 36, 142, 60, 8, 0x1948);
  display.setTextDatum(textdatum_t::top_left);
  display.setTextSize(1);
  display.setTextColor(0x3DFE);
  display.drawString("IA", 176, 42);

  // Row 2 (Y=102..156, H=54, r=8): BAT (12, 92), TEMP (114, 92), UR (216, 92)
  display.fillRoundRect(12, 102, 92, 54, 8, 0x0841);
  display.drawRoundRect(12, 102, 92, 54, 8, 0x1948);
  display.setTextDatum(textdatum_t::top_center);
  display.drawString("BAT", 58, 106);

  display.fillRoundRect(114, 102, 92, 54, 8, 0x0841);
  display.drawRoundRect(114, 102, 92, 54, 8, 0x1948);
  display.setTextDatum(textdatum_t::top_center);
  display.drawString("TEMP", 160, 106);

  display.fillRoundRect(216, 102, 92, 54, 8, 0x0841);
  display.drawRoundRect(216, 102, 92, 54, 8, 0x1948);
  display.setTextDatum(textdatum_t::top_center);
  display.drawString("UR", 262, 106);

  // Row 3 (Y=162..188, H=26, r=13): ToF Pill (36, 248)
  display.fillRoundRect(36, 162, 248, 26, 13, 0x0841);
  display.drawRoundRect(36, 162, 248, 26, 13, 0x1948);

  // Footer Progress Bar Frame (X=16, Y=216, W=288, H=12, r=6)
  display.drawRoundRect(16, 216, 288, 12, 6, 0x3DFE);

  uint32_t startMs = millis();
  uint32_t lastSensorUpdateMs = 0;
  uint32_t lastDisplayUpdateMs = 0;
  uint16_t distMm = 0;
  UnitPaHubManager::EnvData env;
  bool wifiConnectedBefore = false;
  bool geminiConnectTriggered = false;

  constexpr uint32_t kMinDashboardMs = 1800; // ~1.8 s minimum time
  constexpr uint32_t kNoWifiMaxMs = 8000;    // ~8 s ceiling if Wi-Fi cannot connect

  while (millis() - startMs < durationMs) {
    M5StackChan.update();
    uint32_t now = millis();
    uint32_t elapsed = now - startMs;
    uint32_t elapsedSec = elapsed / 1000;
    uint32_t totalSec = durationMs / 1000;

    // Check touch skip on screen (after ~300ms)
    auto t = M5.Touch.getDetail();
    if (elapsed > 300 && t.wasClicked()) {
      Serial.printf("BootDashboard: skipped by user touch click at elapsed=%lu\n", elapsed);
      unitPaHub.click();
      break;
    }

    // Check PET / top touch during dashboard (after ~300ms)
    auto touchIntensities = M5StackChan.TouchSensor.getIntensities();
    bool topTouched = (touchIntensities[0] > 0 || touchIntensities[1] > 0 || touchIntensities[2] > 0 || M5StackChan.TouchSensor.wasSwiped());
    if (elapsed > 300 && topTouched) {
      Serial.printf("BootDashboard: skipped by top touch/pet at elapsed=%lu\n", elapsed);
      g_pending_boot_pet = true;
      unitPaHub.click();
      break;
    }

    // Handle Wi-Fi connection concurrently (100% non-blocking)
    bool isWifi = (WiFi.status() == WL_CONNECTED);
    if (!wifiConnectedBefore && isWifi) {
      wifiConnectedBefore = true;
      WiFi.setSleep(false);
      WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
                  IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
      Serial.printf("WiFi: connected during boot screen! ip=%s dns=%s\n",
                    WiFi.localIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
      configureNtpOnce();
      webConfig.begin(configManager.webConfig());
      if (geminiConfigured && !geminiConnectTriggered) {
        geminiConnectTriggered = true;
        Serial.println("Gemini: connecting WebSocket concurrently during boot screen...");
        gemini.connect();
      }
    }

    // Early exit conditions after minimum time (~1.8s)
    if (elapsed >= kMinDashboardMs) {
      bool geminiReadyOrNotNeeded = !geminiConfigured || gemini.isReady();
      if (isWifi && geminiReadyOrNotNeeded) {
        Serial.printf("BootDashboard: early exit (WiFi + Gemini ready) at elapsed=%lu\n", elapsed);
        break;
      }
      if (!isWifi && elapsed >= kNoWifiMaxMs) {
        Serial.printf("BootDashboard: early exit (no-WiFi ceiling %u ms) at elapsed=%lu\n",
                      static_cast<unsigned>(kNoWifiMaxMs), elapsed);
        break;
      }
    }

    // Keep WebSocket and WebConfig active concurrently during boot screen
    if (isWifi) {
      gemini.loop();
      webConfig.loop();
    }

    // Refresh sensor data every 200 ms
    if (now - lastSensorUpdateMs >= 200) {
      lastSensorUpdateMs = now;
      if (unitPaHub.isFound()) {
        unitPaHub.readSensors(distMm, env);
        if (env.valid) {
          g_latest_temp = env.temperature;
          g_latest_hum = env.humidity;
          g_latest_pressure = env.pressure;
        }
      }
    }

    // Refresh dynamic UI elements every 100 ms
    if (now - lastDisplayUpdateMs >= 100) {
      lastDisplayUpdateMs = now;

      // 1. Wi-Fi Card Dynamic Elements
      display.fillRect(20, 56, 128, 36, 0x0841);
      display.fillRect(58, 42, 90, 14, 0x0841);

      // Signal Bars
      uint16_t barColor = isWifi ? 0x07E0 : 0x4208;
      display.fillRect(24, 76, 3, 6, barColor);
      display.fillRect(29, 72, 3, 10, barColor);
      display.fillRect(34, 68, 3, 14, barColor);
      display.fillRect(39, 64, 3, 18, barColor);

      display.setTextDatum(textdatum_t::top_left);
      display.setTextSize(1);
      if (isWifi) {
        display.setTextColor(0x07E0);
        display.drawString("OK", 62, 42);
        display.setTextColor(TFT_WHITE);
        display.drawString(WiFi.localIP().toString().c_str(), 48, 66);
      } else {
        display.setTextColor(0xFFE0);
        display.drawString("AGUARDANDO", 62, 42);
        display.setTextColor(0x8410);
        display.drawString("Conectando...", 48, 66);
      }

      // 2. IA Gemini Card Dynamic Elements
      display.fillRect(174, 54, 130, 38, 0x0841);
      display.fillRect(198, 42, 106, 14, 0x0841);

      if (geminiConfigured && gemini.isReady()) {
        display.drawCircle(186, 68, 8, 0x03E0);
        display.fillCircle(186, 68, 5, 0x07E0);
        display.fillCircle(186, 68, 2, TFT_WHITE);
        display.setTextColor(0x07E0);
        display.drawString("PRONTA", 204, 42);
        display.setTextColor(TFT_WHITE);
        String mName = configManager.config().geminiModel;
        if (mName.startsWith("models/")) mName = mName.substring(7);
        if (mName.length() > 11) mName = mName.substring(0, 11);
        display.drawString(mName.c_str(), 202, 66);
      } else if (isWifi && geminiConfigured) {
        display.drawCircle(186, 68, 8, 0x8400);
        display.fillCircle(186, 68, 5, 0xFFE0);
        display.fillCircle(186, 68, 2, TFT_WHITE);
        display.setTextColor(0xFFE0);
        display.drawString("CONECTANDO", 204, 42);
        display.setTextColor(0x8410);
        display.drawString("Live WS...", 202, 66);
      } else {
        display.drawCircle(186, 68, 8, 0x4000);
        display.fillCircle(186, 68, 5, 0xF800);
        display.fillCircle(186, 68, 2, 0x8000);
        display.setTextColor(0xF800);
        display.drawString("OFFLINE", 204, 42);
        display.setTextColor(0x8410);
        display.drawString("Desconectado", 202, 66);
      }

      // 3. BAT Card Dynamic Elements
      display.fillRect(16, 120, 84, 32, 0x0841);
      float batVolts = M5StackChan.getBatteryVoltage();
      int batLevel = M5.Power.getBatteryLevel();
      uint8_t batPct = (batLevel >= 0) ? static_cast<uint8_t>(batLevel) :
                       (batVolts > 3.4f ? static_cast<uint8_t>(constrain((batVolts - 3.4f) / 0.8f * 100.0f, 0.0f, 100.0f)) : 0);
      uint16_t batColor = (batPct > 40) ? 0x07E0 : ((batPct > 20) ? 0xFFE0 : 0xF800);
      display.drawRect(20, 126, 20, 12, TFT_WHITE);
      display.fillRect(40, 129, 2, 6, TFT_WHITE);
      int fillW = constrain(static_cast<int>((batPct * 16) / 100), 0, 16);
      if (fillW > 0) display.fillRect(22, 128, fillW, 8, batColor);
      char bStr[12];
      snprintf(bStr, sizeof(bStr), "%u%%", batPct);
      display.setTextDatum(textdatum_t::middle_left);
      display.setTextColor(batColor);
      display.drawString(bStr, 48, 132);

      // 4. TEMP Card Dynamic Elements
      display.fillRect(118, 120, 84, 32, 0x0841);
      display.drawRoundRect(124, 124, 4, 10, 1, TFT_WHITE);
      display.drawCircle(126, 136, 4, TFT_WHITE);
      display.fillCircle(126, 136, 3, 0xFD40);
      display.fillRect(125, 127, 2, 6, 0xFD40);
      char tStr[16];
      if (unitPaHub.isENVReady() && env.valid) {
        snprintf(tStr, sizeof(tStr), "%.1f C", env.temperature);
      } else {
        snprintf(tStr, sizeof(tStr), "-- C");
      }
      display.setTextDatum(textdatum_t::middle_left);
      display.setTextColor(TFT_WHITE);
      display.drawString(tStr, 136, 132);

      // 5. UR Card Dynamic Elements
      display.fillRect(220, 120, 84, 32, 0x0841);
      display.fillCircle(228, 135, 4, 0x05BF);
      display.fillTriangle(224, 134, 232, 134, 228, 125, 0x05BF);
      char hStr[16];
      if (unitPaHub.isENVReady() && env.valid) {
        snprintf(hStr, sizeof(hStr), "%.0f%%", env.humidity);
      } else {
        snprintf(hStr, sizeof(hStr), "--%%");
      }
      display.setTextDatum(textdatum_t::middle_left);
      display.setTextColor(TFT_WHITE);
      display.drawString(hStr, 240, 132);

      // 6. ToF Pill Dynamic Elements (Double Arrow Graphic + cm)
      display.fillRect(48, 166, 224, 18, 0x0841);
      if (unitPaHub.isTOFReady()) {
        display.fillTriangle(106, 171, 106, 179, 100, 175, 0x07E0);
        display.fillTriangle(144, 171, 144, 179, 150, 175, 0x07E0);
        display.drawLine(103, 175, 147, 175, 0x07E0);
        char distBuf[16];
        if (distMm > 0) {
          snprintf(distBuf, sizeof(distBuf), "%u cm", distMm / 10);
        } else {
          snprintf(distBuf, sizeof(distBuf), "-- cm");
        }
        display.setTextDatum(textdatum_t::middle_left);
        display.setTextColor(0x07E0);
        display.drawString(distBuf, 160, 175);
      } else {
        display.setTextDatum(textdatum_t::middle_center);
        display.setTextColor(0x8410);
        display.drawString("Sensor ToF AUSENTE", 160, 175);
      }

      // 7. Footer Countdown & Text
      display.fillRect(20, 196, 280, 14, TFT_BLACK);
      display.setTextDatum(textdatum_t::middle_center);
      display.setTextColor(0x9DF8);
      char footerStr[48];
      snprintf(footerStr, sizeof(footerStr), "%u / %u s    Toque para continuar", elapsedSec, totalSec);
      display.drawString(footerStr, 160, 203);

      // 8. Progress Bar Fill
      int barW = constrain(static_cast<int>((elapsed * 282) / durationMs), 0, 282);
      if (barW > 0) display.fillRoundRect(19, 219, barW, 6, 3, 0x07E0);
    }

    delay(20);
  }

  // Clear before handing off to procedural face
  display.fillScreen(TFT_BLACK);
  Serial.println("BootDashboard: dashboard screen finished.");
}

void setup() {
  // CRITICAL HARDWARE SAFETY: Force Port B Vibrator DIN (GPIO 9) to LOW immediately on reset
  // so the motor transistor cannot conduct when the 5V rail powers on!
  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);
  pinMode(8, INPUT);

  Serial.begin(115200);
  Serial.printf("[DIAG:BOOT] setup entry millis=%lu\n", millis());
  esp_reset_reason_t rstReason = esp_reset_reason();
  Serial.printf("CoreS3 BOOT: reset_reason=%d\n", static_cast<int>(rstReason));
  M5StackChan.begin();
  ConfigManager::applyTimezone(-3);
  if (M5.Rtc.isEnabled()) {
    M5.Rtc.setSystemTimeFromRtc();
  }
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
  servoGestures.queueGesture("center_head");
  {
    Preferences servoPrefs;
    servoPrefs.begin("servo", true);
    int nvsZero1 = servoPrefs.getInt("zero_pos_1", -1);
    int nvsZero2 = servoPrefs.getInt("zero_pos_2", -1);
    servoPrefs.end();
    auto bspAng = M5StackChan.Motion.getCurrentAngles();
    Serial.printf("[DIAG:PITCH_BOOT] NVS zero_pos_1(yaw)=%d zero_pos_2(pitch)=%d BSP angX=%d angY=%d\n",
                  nvsZero1, nvsZero2, bspAng.x, bspAng.y);
  }
  if (unitPaHub.begin()) {
    for (uint8_t channel = 0; channel < 8; ++channel) {
      unitPaHub.scanChannel(channel);
    }
  }
  initLtr553();
  liveFeeds.setGeminiLiveProbe(&gemini);
  liveFeeds.setGoogleCalendar(&googleCalendar);
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
  toolBridge.setConfigManager(&configManager);
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
    // Step 4: Temporarily disable calendar network fetch by preserving URL to .bak and emptying file
    if (SD.exists("/app/StackChan/secrets/calendar_url.txt")) {
      File fCur = SD.open("/app/StackChan/secrets/calendar_url.txt", FILE_READ);
      if (fCur && fCur.size() > 0) {
        String existingUrl = fCur.readString();
        fCur.close();
        File fBak = SD.open("/app/StackChan/secrets/calendar_url.txt.bak", FILE_WRITE);
        if (fBak) {
          fBak.print(existingUrl);
          fBak.close();
        }
        File fEmpty = SD.open("/app/StackChan/secrets/calendar_url.txt", FILE_WRITE);
        if (fEmpty) {
          fEmpty.close();
          Serial.println("[GoogleCalendar] calendar_url.txt backed up to .bak and emptied for recovery.");
        }
      } else if (fCur) {
        fCur.close();
      }
    }
    ConfigManager::applyTimezone(configManager.config().timezoneOffset);
    googleCalendar.refreshCachedUrl();
    if (M5.Rtc.isEnabled()) {
      M5.Rtc.setSystemTimeFromRtc();
    }
    configManager.printRedactedStatus(Serial);
    g_speaker_volume = configManager.config().speakerVolume;
    configureAudio(8, 0);
    applySpeakerVolume();
    Serial.printf("Audio: speaker_volume=%u mic_magnification=8 mic_noise_filter=0 (Phase 3 operational)\n",
                  static_cast<unsigned>(g_speaker_volume));
    toolGateway.begin(configManager.gatewayConfig());

    // 1. Pre-configure Gemini live probe session parameters
    const auto& cfg = configManager.config();
    bool geminiConfigured = false;
    if (cfg.geminiEnabled && configManager.hasGeminiApiKey()) {
      Serial.println("Gemini: configuring live session...");
      gemini.setModel(cfg.geminiModel);
      gemini.setVoiceName(cfg.geminiVoice);
      gemini.setVadConfig(cfg.vadPrefixPaddingMs, cfg.vadSilenceDurationMs,
                          cfg.vadStartSensitivityHigh, // Patch 4 audio stable: START_SENSITIVITY_HIGH
                          cfg.vadEndSensitivityLow,
                          cfg.vadTurnIncludesAllInput);
      gemini.setSystemPrompt(cfg.systemPrompt);
      gemini.setPersonaPrompt(cfg.personaPrompt);
      geminiConfigured = gemini.begin(configManager.readGeminiApiKey().c_str());
      Serial.printf("Gemini: begin %s\n", geminiConfigured ? "ok" : "failed");
    }

    // 2. Start Wi-Fi connection asynchronously in background
    bool wifiStarted = startWifiAsync(configManager);

    // 3. Show Graphical Boot Dashboard Screen (up to 15s max, concurrent non-blocking connection)
    Serial.printf("[DIAG:BOOT] dashboard start millis=%lu\n", millis());
    showBootDashboardScreen(15000, geminiConfigured);
    Serial.printf("[DIAG:BOOT] dashboard end millis=%lu isWifi=%d geminiReady=%d\n",
                  millis(), (int)(WiFi.status() == WL_CONNECTED), (int)gemini.isReady());

    // 4. Post-boot screen finalization
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);
    if (wifi_ok) {
      configureNtpOnce();
      if (configManager.config().webEnabled) {
        auto webOpts = configManager.webConfig();
        webConfig.begin(webOpts);
        Serial.printf("WebConfig: started in STA mode on port %u\n", webOpts.port);
      }
      if (geminiConfigured && gemini.isReady()) {
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = millis() + 3000;
        Serial.println("Boot: playing wake droid chirp...");
        playWakeDroidChirp();
        uint32_t chirpWait = 0;
        while (M5.Speaker.isPlaying() && chirpWait < 350) {
          delay(10);
          chirpWait += 10;
        }
        Serial.println("Boot: Gemini ready. Conversation starts DISARMED -> wave or touch to talk");
      } else {
        emotion.setEmotion("neutral");
      }
    } else {
      bool setup_ap_ok = false;
      if (shouldStartSetupAccessPoint(configManager, runtime_config_exists)) {
        setup_ap_ok = startSetupAccessPoint(configManager);
      }
      if (setup_ap_ok) {
        WebConfigServer::Config setupWeb;
        setupWeb.enabled = true;
        setupWeb.port = 80;
        setupWeb.robotId = configManager.config().robotId.c_str();
        webConfig.begin(setupWeb);
      } else {
        WebConfigServer::Config webOff;
        webOff.enabled = false;
        webOff.robotId = configManager.config().robotId.c_str();
        webConfig.begin(webOff);
      }
      emotion.setEmotion("neutral");
    }
  }

  // Gemini API key is loaded by ConfigManager as a secret and must never be printed.
  g_last_human_activity_ms = millis();
  Serial.printf("[DIAG:BOOT] setup exit millis=%lu g_last_human_activity_ms=%lu\n", millis(), g_last_human_activity_ms);
}

void loop() {
  static bool s_first_loop = true;
  if (s_first_loop) {
    s_first_loop = false;
    Serial.printf("[LOOP_ENTRY] millis=%lu freeHeap=%u maxAllocHeap=%u last_activity=%lu idle_age=%lu emo=%s mute=%d dnd=%d\n",
                  millis(), static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
                  g_last_human_activity_ms, millis() - g_last_human_activity_ms,
                  emotion.currentEmotion().c_str(), (int)g_physical_mute, (int)gemini.isDoNotDisturb());
    if (g_pending_boot_pet) {
      g_pending_boot_pet = false;
      Serial.println("[BOOT:PET] Executing deferred boot pet in first loop()");
      g_last_human_activity_ms = millis();
      unitPaHub.purr();
      emotion.setEmotion("meigo");
      g_emotion_override_until_ms = millis() + 3000;
      playCatPurrDocileSound();
      servoGestures.queueGesture("curious_tilt");
      g_behaviorManager.onEvent(BehaviorEvent::TOUCH, millis());
    }
  }
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
    } else if (cmdLine.startsWith("TIME_SIM=")) {
      int simHour = cmdLine.substring(9).toInt();
      g_behaviorManager.setSimulatedHour(static_cast<int8_t>(simHour));
      Serial.printf("[CONFIG:TIME_SIM] simulated_hour=%d effEnergy=%u effCuriosity=%u tBias=%+d\n",
                    simHour, g_behaviorManager.effectiveEnergy(), g_behaviorManager.effectiveCuriosity(),
                    g_behaviorManager.temporalEnergyBias());
    } else if (cmdLine.startsWith("PITCH_TEST=")) {
      int pVal = cmdLine.substring(11).toInt();
      servoGestures.stop();
      Serial.printf("[TEST:PITCH_MANUAL] requested raw pitch=%d\n", pVal);
      M5StackChan.Motion.move(0, pVal, 500);
      delay(400);
      auto ang = M5StackChan.Motion.getCurrentAngles();
      float ax = 0, ay = 0, az = 0;
      M5.Imu.getAccel(&ax, &ay, &az);
      Serial.printf("[TEST:PITCH_MANUAL] BSP feedback: currentYaw=%d currentPitch=%d IMU ax=%.2f ay=%.2f az=%.2f\n",
                    ang.x, ang.y, ax, ay, az);
    } else if (cmdLine.startsWith("GESTURE=")) {
      String gName = cmdLine.substring(8);
      bool ok = servoGestures.queueGesture(gName);
      Serial.printf("[TEST:GESTURE] queued gesture=%s ok=%s active=%s anchorX=%d anchorY=%d\n",
                    gName.c_str(), ok ? "yes" : "no", servoGestures.active() ? "yes" : "no",
                    servoGestures.anchorX(), servoGestures.anchorY());
    } else if (cmdLine == "CAL_REFRESH") {
      Serial.println("[COMMAND] Triggering background calendar refresh on Core 0...");
      liveFeeds.requestCalendarRefresh();
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
    Serial.printf("[DIAG:PWR_BTN] wasClicked=1 mute=%d millis=%lu\n", (int)g_physical_mute, millis());
    if (g_physical_mute) {
      Serial.printf("[DIAG:SLEEP] trigger=BtnPWR_mute millis=%lu\n", millis());
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
  static bool s_last_audio_speaking = false;
  static uint32_t s_last_accounted_user_turn_id = 0;

  if (s_last_audio_speaking && !realAudioSpeaking) {
    uint32_t curTurnId = gemini.currentUserTurnId();
    if (gemini.isConversationArmed() &&
        !g_is_currently_lifted &&
        gemini.turnState() != GeminiLiveProbe::LiveTurnState::TOOL_CALL &&
        curTurnId > s_last_accounted_user_turn_id) {
      s_last_accounted_user_turn_id = curTurnId;
      g_behaviorManager.onEvent(BehaviorEvent::CONVERSATION_TURN_END, now);
    }
  }
  s_last_audio_speaking = realAudioSpeaking;

  emotion.setIsSpeaking(realAudioSpeaking);
  emotion.setMicActive(gemini.isRecording() && !realAudioSpeaking);
  emotion.setDoNotDisturb(gemini.isDoNotDisturb());
  emotion.setPhysicalMute(g_physical_mute);
  emotion.setConversationArmed(gemini.isConversationArmed());
  emotion.setTurnStateIdle(gemini.turnState() == GeminiLiveProbe::LiveTurnState::IDLE);
  emotion.setSocialAttention(g_behaviorManager.isSocialAttentionActive(now));
  emotion.setBehaviorState(g_behaviorManager.effectiveEnergy(),
                           g_behaviorManager.effectiveCuriosity(),
                           g_behaviorManager.effectiveAffection());
  emotion.loop();

  // Update LiveFeedsManager system idle state for Core 0 background worker
  bool isWifiConnected = (WiFi.status() == WL_CONNECTED);
  if (isWifiConnected) {
    configureNtpOnce();
  }
  bool inSleep = (emotion.currentEmotion() == "sleep");
  bool systemIdle = isWifiConnected &&
                    ((inSleep && !realAudioSpeaking && !gemini.isRecording() && !gemini.isTurnBusy()) ||
                     (gemini.turnState() == GeminiLiveProbe::LiveTurnState::IDLE && !realAudioSpeaking && !gemini.isRecording() && !gemini.isTurnBusy()));
  liveFeeds.setSystemIdle(systemIdle);
  liveFeeds.loop();

  // Safe trigger for LiveFeeds boot prefetch: only after Gemini setupComplete, or if Gemini not configured when system is idle
  static bool s_boot_prefetch_triggered = false;
  if (!s_boot_prefetch_triggered && isWifiConnected) {
    bool geminiConfigured = configManager.config().geminiEnabled && configManager.hasGeminiApiKey();
    bool canTrigger = false;
    if (geminiConfigured) {
      if (gemini.isReady()) {
        canTrigger = true;
      }
    } else {
      if (systemIdle) {
        canTrigger = true;
      }
    }
    if (canTrigger) {
      s_boot_prefetch_triggered = true;
      Serial.println("[BOOT_PREFETCH] Triggering boot prefetch safely (Gemini ready or system idle)");
      liveFeeds.triggerBootPrefetch();
    }
  }

  // Opportunistic background calendar refresh trigger on Core 0 (zero TLS work on Core 1)
  static uint32_t s_last_cal_idle_refresh_ms = 0;
  bool calNeedsInitialFetch = (s_last_cal_idle_refresh_ms == 0 && (!googleCalendar.hasCache() || !googleCalendar.isCacheValidForToday()));
  uint32_t calRefreshInterval = (!googleCalendar.hasCache() || !googleCalendar.isCacheValidForToday()) ? 60000 : 1800000;
  if (systemIdle && (calNeedsInitialFetch || (now - s_last_cal_idle_refresh_ms >= calRefreshInterval))) {
    s_last_cal_idle_refresh_ms = now;
    liveFeeds.requestCalendarRefresh();
  }

  // Servos: mantem servoGestures.loop() ativo apenas se houver gesto ja em curso, sem novos gestos
  servoGestures.loop();

  // Abertura assíncrona do microfone após término real do gesto físico do aceno
  static bool s_pending_wave_arm = false;
  if (s_pending_wave_arm) {
    if (g_physical_mute || gemini.isDoNotDisturb()) {
      s_pending_wave_arm = false;
    } else if (!M5.Speaker.isPlaying() && !servoGestures.active()) {
      s_pending_wave_arm = false;
      Serial.println("[WAVE:ARM] Gesto nod_yes finalizado e speaker livre -> abrindo microfone assincronamente!");
      gemini.armConversation("FAST_GESTURE");
    }
  }

  // Despacho de reação sensorial pendente (ex: alívio ao voltar para a mesa) após término da fala anterior
  if (s_pending_reaction.active) {
    if (gemini.isDoNotDisturb()) {
      s_pending_reaction.active = false;
    } else if (!gemini.isTurnBusy()) {
      if (now - s_pending_reaction.queuedMs < 20000) {
        Serial.printf("SensorySpeech [%s] (dispatching deferred phrase): %s\n",
                      s_pending_reaction.eventName ? s_pending_reaction.eventName : "SENSORY",
                      s_pending_reaction.turnText.c_str());
        gemini.requestTextTurn(s_pending_reaction.turnText);
      } else {
        Serial.printf("SensorySpeech [%s]: deferred phrase expired (>20s)\n",
                      s_pending_reaction.eventName ? s_pending_reaction.eventName : "SENSORY");
      }
      s_pending_reaction.active = false;
    }
  }

  // Reset temporary emotional reactions back to neutral after duration
  if (g_emotion_override_until_ms != 0 && now >= g_emotion_override_until_ms) {
    g_emotion_override_until_ms = 0;
    if (!gemini.isSpeaking() && !gemini.isRecording()) {
      emotion.setEmotion("neutral");
    }
  }

  // Update PixBehaviorManager homeostasis / passive decay
  g_behaviorManager.update(now);

  auto canBehaviorAct = [&]() {
    return !gemini.isConversationArmed() &&
           (gemini.turnState() == GeminiLiveProbe::LiveTurnState::IDLE) &&
           !gemini.isSpeaking() &&
           !gemini.isRecording() &&
           (gemini.turnState() != GeminiLiveProbe::LiveTurnState::TOOL_CALL) &&
           !g_is_currently_lifted &&
           (emotion.currentEmotion() != "scared") &&
           (emotion.currentEmotion() != "sleep") &&
           (g_emotion_override_until_ms == 0);
  };

  auto executeBehaviorDecision = [&](BehaviorDecision decision, uint32_t nowMs) {
    switch (decision) {
      case BehaviorDecision::LOOK_AT_USER:
        servoGestures.queueGesture("look_at_user");
        break;
      case BehaviorDecision::HAPPY_SHORT:
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = nowMs + 2000;
        break;
      case BehaviorDecision::CURIOUS_TILT:
        servoGestures.queueGesture("curious_tilt");
        break;
      case BehaviorDecision::MEIGO_SHORT:
        emotion.setEmotion("meigo");
        g_emotion_override_until_ms = nowMs + 2500;
        unitPaHub.purr();
        servoGestures.queueGesture("curious_tilt");
        playCatPurrDocileSound();
        break;
      case BehaviorDecision::IDLE_BREATH:
        if (g_behaviorManager.effectiveEnergy() < 40) {
          servoGestures.queueGesture("idle_breath_calm");
        } else if (g_behaviorManager.effectiveCuriosity() >= 65) {
          servoGestures.queueGesture("idle_breath_curious");
        } else {
          servoGestures.queueGesture("idle_breath");
        }
        break;
      case BehaviorDecision::SOCIAL_MICRO_ADJUST:
        servoGestures.queueGesture("social_micro_adjust");
        break;
      case BehaviorDecision::NO_ACTION:
      default:
        break;
    }
  };

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
                      ax, ay, az, totalG, gyroMag, jerk, g_is_currently_lifted ? 1 : 0, servoMoving ? 1 : 0);
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
          g_is_currently_lifted = false;
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
            g_is_currently_lifted = true;
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
          g_is_currently_lifted = true;
          // RETORNO A MESA: superficie calma sustentada por 800ms
          if (isCalmOnTable) {
            if (tableStableSinceMs == 0) tableStableSinceMs = now;
            else if (now - tableStableSinceMs >= 800) {
              s_motionState = EilikMotionState::RESTING;
              tableStableSinceMs = 0;
              g_is_currently_lifted = false;
              emotion.setLifted(false);
              g_behaviorManager.onEvent(BehaviorEvent::LIFT_RETURN, now);
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
        Serial.println("Action: Conversa já ativa -> renovação de escuta (feedback visual discreto)");
        g_last_human_activity_ms = now;
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = now + 2000;
        gemini.armConversation("FAST_GESTURE");
        g_behaviorManager.onEvent(BehaviorEvent::WAVE, now);
      }
      // Prioridade 5: Gesto válido para abrir conversa!
      else {
        Serial.println("Action: Gesto aceito -> Acordando e preparando microfone!");
        g_last_human_activity_ms = now;
        playWakeDroidChirp();
        emotion.setEmotion("happy");
        g_emotion_override_until_ms = now + 2500;
        unitPaHub.click();
        servoGestures.queueGesture("nod_yes");
        s_pending_wave_arm = true;
        g_behaviorManager.onEvent(BehaviorEvent::WAVE, now);
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
      } else if (canBehaviorAct()) {
        BehaviorDecision dec = g_behaviorManager.onEvent(BehaviorEvent::PRESENCE, now);
        executeBehaviorDecision(dec, now);
      }
    }
    else if (tofEvt == TofEvent::USER_LEFT) {
      Serial.println("TofEvent: USER_LEFT (ausência contínua >15s confirmada)");
      // Mesa vazia: kIdleSleepMs (120s) cuidará de colocar o robô para dormir suavemente
    }
  }

  // Presença contínua à mesa: mantém o robô acordado enquanto o usuário estiver trabalhando
  bool isUserPres = g_tofDetector.isUserPresent();
  g_behaviorManager.setUserPresent(isUserPres);
  if (isUserPres) {
    g_last_human_activity_ms = now;
  }

  // ENV III: Telemetria ambiental cooperativa e não-bloqueante (cadência ~6000ms).
  // Sequência SHT30 não bloqueia loop (TRIGGER -> wait >=25ms -> COLLECT + QMP6988).
  // Suspende/aborta ciclo se houver gravação de mic ou reprodução de fala.
  static uint32_t s_last_env_cycle_ms = 0;
  bool audioBusy = gemini.isSpeaking() || gemini.isRecording();

  if (audioBusy && unitPaHub.isEnvTriggered()) {
    unitPaHub.abortEnvMeasurement();
  }

  if (unitPaHub.isFound() && !audioBusy) {
    if (!unitPaHub.isEnvTriggered()) {
      if (now - s_last_env_cycle_ms >= 6000) {
        s_last_env_cycle_ms = now;
        unitPaHub.triggerEnvMeasurement();
      }
    } else {
      if (unitPaHub.envTriggerAgeMs() >= 25) {
        UnitPaHubManager::EnvData env;
        if (unitPaHub.collectEnvMeasurement(env)) {
          if (env.valid) {
            g_latest_temp = env.temperature;
            g_latest_hum = env.humidity;
            g_latest_pressure = env.pressure;
          }
          Serial.printf("Units: env=%s temp_c=%.2f humidity=%.2f pressure_hpa=%.2f\n",
                        env.valid ? "ok" : "missing",
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
        }
      }
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
          Serial.printf("[DIAG:SLEEP] trigger=LTR553_darkness lux=%u millis=%lu\n", alsAvg, millis());
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
            if (servoGestures.active()) servoGestures.stop();
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

  static uint32_t s_last_diag_touch_ms = 0;
  if ((frontTouch > 0 || midTouch > 0 || backTouch > 0 || swiped) && (now - s_last_diag_touch_ms > 200)) {
    s_last_diag_touch_ms = now;
    Serial.printf("[DIAG:TOUCH_SI12T] physical front=%u mid=%u back=%u swiped=%d millis=%lu\n",
                  frontTouch, midTouch, backTouch, (int)swiped, now);
  }

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
        Serial.printf("[DIAG:PET_DISPATCH] duration=%u emotion=%s mute=%d dnd=%d canBehaviorAct=%d millis=%lu\n",
                      petDuration, emotion.currentEmotion().c_str(), (int)g_physical_mute,
                      (int)gemini.isDoNotDisturb(), (int)canBehaviorAct(), now);
        Serial.printf("Touch: middle affectionate pet (duration=%u ms)\n", petDuration);
        if (emotion.currentEmotion() == "sleep" && !g_physical_mute && !gemini.isDoNotDisturb()) {
          unitPaHub.purr();
          emotion.setEmotion("meigo");
          g_emotion_override_until_ms = now + 3500;
          servoGestures.queueGesture("curious_tilt");
          playCatPurrDocileSound();
          playWakeDroidChirp();
          g_behaviorManager.onEvent(BehaviorEvent::TOUCH, now);
        } else {
          // Todo afago no eletrodo central deve sempre gerar:
          // emotion = "meigo"
          // playCatPurrDocileSound()
          // feedback háptico purr
          // O que pode variar suavemente é apenas o gesto físico: curious_tilt ou nod_yes
          unitPaHub.purr();
          emotion.setEmotion("meigo");
          g_emotion_override_until_ms = now + 3000;
          playCatPurrDocileSound();

          static bool s_pet_gesture_toggle = false;
          s_pet_gesture_toggle = !s_pet_gesture_toggle;
          const char* petGesture = s_pet_gesture_toggle ? "curious_tilt" : "nod_yes";
          servoGestures.queueGesture(petGesture);

          g_behaviorManager.onEvent(BehaviorEvent::TOUCH, now);
          Serial.printf("[TOUCH:TOP] afago central -> emotion=meigo sound=purr haptic=purr gesture=%s\n", petGesture);
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
          if (servoGestures.active()) servoGestures.stop();
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
  static uint32_t s_drawer_opened_at_ms = 0;

  auto updateQuickStatusData = [&]() {
    int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
    float batVolts = M5StackChan.getBatteryVoltage();
    int batLevel = M5.Power.getBatteryLevel();
    uint8_t batPct = (batLevel >= 0) ? static_cast<uint8_t>(batLevel) :
                     (batVolts > 3.4f ? static_cast<uint8_t>(constrain((batVolts - 3.4f) / 0.8f * 100.0f, 0.0f, 100.0f)) : 0);
    bool charging = (M5StackChan.getBatteryCurrent() < -0.01f) || M5.Power.isCharging();
    uint8_t iaState = 0;
    if (gemini.isReady()) {
      iaState = 2; // IA ON
    } else if (gemini.isConnected()) {
      iaState = 1; // IA ...
    }
    emotion.setQuickStatusData(rssi, ip, batPct, batVolts, charging, iaState, g_latest_temp, g_latest_hum, g_current_distance_mm);
  };

  auto t = M5.Touch.getDetail();
  static uint32_t s_last_diag_screen_touch_ms = 0;
  if ((t.isPressed() || t.wasPressed() || t.wasReleased() || t.wasClicked()) && (now - s_last_diag_screen_touch_ms > 100)) {
    s_last_diag_screen_touch_ms = now;
    Serial.printf("[DIAG:TOUCH_SCREEN] isPressed=%d wasPressed=%d wasReleased=%d wasClicked=%d x=%d y=%d millis=%lu\n",
                  (int)t.isPressed(), (int)t.wasPressed(), (int)t.wasReleased(), (int)t.wasClicked(), t.x, t.y, now);
  }
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
      s_drawer_opened_at_ms = now;
      Serial.printf("Display: SWIPE DOWN drawer activated (start_y=%d cur_y=%d dy=%d dx=%d)\n", s_touch_start_y, t.y, dy, dx);
      unitPaHub.click();
      updateQuickStatusData();
      emotion.showQuickStatus(6000);
    }
  }

  if ((t.wasReleased() || (!t.isPressed() && s_screen_touching)) && s_screen_touching) {
    s_screen_touching = false;
    uint32_t touchDuration = now - s_touch_start_ms;
    int16_t totalDx = abs(t.x - s_touch_start_x);
    int16_t totalDy = t.y - s_touch_start_y;

    // Check if release completes a swipe down that wasn't caught dynamically
    if (!s_drawer_triggered && s_touch_start_y < 175 && totalDy >= 20 && totalDy > (totalDx / 2)) {
      s_drawer_triggered = true;
      s_drawer_opened_at_ms = now;
      Serial.printf("Display: SWIPE DOWN drawer activated on release (start_y=%d end_y=%d dy=%d dx=%d)\n", s_touch_start_y, t.y, totalDy, totalDx);
      unitPaHub.click();
      updateQuickStatusData();
      emotion.showQuickStatus(6000);
    }

    if (!s_drawer_triggered) {
      // Valid tap criteria: |dx| < 12 && |dy| < 12 && duration < 400 ms
      bool isValidTap = (totalDx < 12) && (abs(totalDy) < 12) && (touchDuration < 400);
      Serial.printf("[DIAG:TOUCH_SCREEN_DISPATCH] isValidTap=%d totalDx=%d totalDy=%d duration=%lu drawer=%d millis=%lu\n",
                    (int)isValidTap, totalDx, totalDy, touchDuration, (int)emotion.isQuickStatusActive(), now);

      if (isValidTap) {
        if (emotion.isQuickStatusActive()) {
          // Anti-bounce: ignore close events within 600 ms of drawer opening
          if (now - s_drawer_opened_at_ms >= 600) {
            emotion.hideQuickStatus();
            Serial.println("Display: quick status drawer dismissed by deliberate tap");
          } else {
            Serial.println("Display: tap ignored during drawer open 600ms anti-bounce cooldown");
          }
        } else {
          // Normal screen tap
          Serial.printf("Display touch click at x=%d y=%d (duration=%lums dx=%d dy=%d)\n", t.x, t.y, touchDuration, totalDx, totalDy);
          dialogueToggle = true;
        }
      } else {
        // Short swipe or drag that is neither a tap nor a valid swipe: ignore (do not trigger tap)
        Serial.printf("Display touch discarded (duration=%lums dx=%d dy=%d)\n", touchDuration, totalDx, totalDy);
      }
    }
    s_drawer_triggered = false;
  }

  // Periodic sensor/status refresh while drawer is actively displayed
  if (emotion.isQuickStatusActive()) {
    static uint32_t s_last_drawer_data_update_ms = 0;
    if (now - s_last_drawer_data_update_ms >= 200) {
      s_last_drawer_data_update_ms = now;
      updateQuickStatusData();
    }
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
        if (servoGestures.active()) servoGestures.stop();
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

  // Idle micro-movement / breathing simulation / spontaneous behaviors via PixBehaviorManager
  if (canBehaviorAct() && !servoGestures.active()) {
    BehaviorDecision dec = g_behaviorManager.onEvent(BehaviorEvent::IDLE_TICK, now);
    executeBehaviorDecision(dec, now);
  }

  static uint32_t s_last_30s_diag_ms = 0;
  if (now < 60000 && now - s_last_30s_diag_ms >= 1000) {
    s_last_30s_diag_ms = now;
    Serial.printf("[DIAG:STATE30S] t=%lu lastAct=%lu diff=%lu sysIdle=%d gemTurn=%d dnd=%d mute=%d lifted=%d ovr=%lu emo=%s liveInFlight=%d calInFlight=%d canAct=%d\n",
                  now, g_last_human_activity_ms, now - g_last_human_activity_ms,
                  (int)systemIdle, (int)gemini.turnState(), (int)gemini.isDoNotDisturb(),
                  (int)g_physical_mute, (int)g_is_currently_lifted,
                  (unsigned long)g_emotion_override_until_ms, emotion.currentEmotion().c_str(),
                  (int)liveFeeds.isRequestInFlight(), (int)liveFeeds.isCalendarJobInFlight(),
                  (int)canBehaviorAct());
  }

  if (!gemini.isRecording() && !gemini.isSpeaking() && !gemini.continuousConversation() &&
      emotion.currentEmotion() != "sleep" &&
      g_emotion_override_until_ms == 0 &&
      g_last_human_activity_ms &&
      now - g_last_human_activity_ms > kIdleSleepMs) {
    Serial.printf("[DIAG:SLEEP] trigger=idle_timeout now=%lu last_activity=%lu diff=%lu kIdleSleepMs=%lu millis=%lu\n",
                  now, g_last_human_activity_ms, now - g_last_human_activity_ms, kIdleSleepMs, millis());
    emotion.setEmotion("sleep");
    servoGestures.queueGesture("center_head");
  }

  // Durante a fala, roda livre sem delay(2) para maxima frequencia de atendimento ao speaker DMA
  if (!gemini.isSpeaking()) {
    delay(2);
  }
}
