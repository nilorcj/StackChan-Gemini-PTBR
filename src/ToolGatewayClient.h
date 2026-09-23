#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

// Lightweight client for a local StackChan Tool/MCP Gateway.
// The ESP32 does not run MCP servers directly. It calls one LAN gateway that
// exposes a curated tool list and routes calls to Hermes/MCP backends.
class ToolGatewayClient {
 public:
  struct Config {
    String baseUrl = "";
    String robotId = "stackchan";
    uint32_t timeoutMs = 8000;
    size_t maxResponseBytes = 8192;
    bool enabled = false;  // keep disabled until Wi-Fi/config is wired.
  };

  struct CallResult {
    bool ok = false;
    int httpCode = 0;
    String body;
    String error;
  };

  bool begin(const Config& config);
  bool isEnabled() const { return config_.enabled; }
  const String& baseUrl() const { return config_.baseUrl; }

  // GET /tools -> compact Gemini function declarations or gateway-native tool list.
  CallResult fetchTools(String* toolsJsonOut = nullptr);

  // POST /call {tool, arguments, robot_id, session_id}
  CallResult callTool(const String& toolName, const JsonVariantConst& arguments,
                      const String& sessionId = "");

  // Local static fallback for compile/runtime smoke tests when gateway is absent.
  String defaultToolDeclarationsJson() const;

 private:
  Config config_;

  String joinUrl(const char* suffix) const;
  CallResult getJson(const String& url);
  CallResult postJson(const String& url, const String& payload);
  String sanitizeForLog(const String& input) const;
};
