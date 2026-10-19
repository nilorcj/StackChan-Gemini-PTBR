#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include "ToolGatewayClient.h"
#include "WebConfigServer.h"

// Loads StackChan option-B runtime configuration from SD without printing secrets.
// Secret values can be read by callers that need them, but status/log helpers expose only set/missing.
class ConfigManager {
 public:
  struct RuntimeConfig {
    String robotId = "stackchan";
    String geminiModel = "models/gemini-3.1-flash-live-preview";
    String geminiVoice = "Aoede";
    bool wifiEnabled = false;
    bool geminiEnabled = false;
    bool gatewayEnabled = false;
    bool webEnabled = false;
    String gatewayBaseUrl;
    String wifiSsid;
    uint8_t speakerVolume = 200;
    uint8_t micMagnification = 8;
    uint8_t micNoiseFilterLevel = 0;
    uint16_t vadPrefixPaddingMs = 200;
    uint16_t vadSilenceDurationMs = 280;
    bool vadStartSensitivityHigh = false; // START_SENSITIVITY_LOW (Consolidated post-audio)
    bool vadEndSensitivityLow = false; // END_SENSITIVITY_HIGH for immediate conversational turn
    bool vadTurnIncludesAllInput = true;
    int8_t timezoneOffset = -3;
    String timezoneName = "UTC-3";
    String systemPrompt;
    String personaPrompt;
  };

  explicit ConfigManager(fs::FS& fs);

  bool begin();
  bool load();

  const RuntimeConfig& config() const { return config_; }
  bool ready() const { return ready_; }

  bool hasGeminiApiKey() const;
  bool hasGatewayToken() const;
  bool hasGatewayHmacKey() const;
  bool hasWifiPassword() const;
  bool hasCalendarUrl() const;
  String readGeminiApiKey() const;
  String readGatewayToken() const;
  String readGatewayHmacKey() const;
  String readWifiPassword() const;
  String readCalendarUrl() const;

  ToolGatewayClient::Config gatewayConfig() const;
  WebConfigServer::Config webConfig() const;

  String redactedStatusJson() const;
  void printRedactedStatus(Stream& out) const;

  static void applyTimezone(int8_t offsetHours);

 private:
  fs::FS& fs_;
  RuntimeConfig config_;
  bool ready_ = false;

  bool ensureDirs();
  bool loadJsonConfig(const char* path);
  void loadPrompts();
  String readTextFile(const char* path, size_t maxBytes = 4096) const;
  bool fileExists(const char* path) const;
  static bool asBool(JsonVariantConst v, bool fallback);
};
