#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "ToolGatewayClient.h"
#include "EmotionController.h"
#include "ServoGestureController.h"
#include "CameraCapture.h"

class GeminiLiveProbe;
class MemoryStore;
class UnitPaHubManager;
class LiveFeedsManager;
class ConfigManager;

// Bridges Gemini function calls to local robot handlers or the LAN Tool/MCP Gateway.
// This is intentionally small: the gateway does MCP discovery/auth/schema filtering.
class GeminiToolBridge {
 public:
  enum class LocalStatus {
    NotHandled,
    Handled,
    Error,
  };

  GeminiToolBridge(ToolGatewayClient& gateway, EmotionController& emotion,
                   ServoGestureController& servoGestures, CameraCapture& camera)
      : gateway_(gateway), emotion_(emotion), servoGestures_(servoGestures), camera_(camera) {}

  void setGemini(GeminiLiveProbe* gemini) { gemini_ = gemini; }
  void setMemoryStore(MemoryStore* memory) { memory_ = memory; }
  void setUnitPaHub(UnitPaHubManager* unitPaHub) { unitPaHub_ = unitPaHub; }
  void setLiveFeeds(LiveFeedsManager* feeds) { liveFeeds_ = feeds; }
  void setGoogleCalendar(class GoogleCalendarClient* cal) { googleCalendar_ = cal; }
  void setConfigManager(ConfigManager* cfg) { configManager_ = cfg; }

  String functionDeclarationsJson();
  String handleFunctionCall(const String& name, const JsonVariantConst& args,
                            const String& sessionId = "");

  bool executeFastPathGesture(const String& gesture, uint32_t turnId, const String& hold = "normal");
  void invalidatePendingLocalAction();

 private:
  ToolGatewayClient& gateway_;
  EmotionController& emotion_;
  ServoGestureController& servoGestures_;
  CameraCapture& camera_;
  GeminiLiveProbe* gemini_ = nullptr;
  MemoryStore* memory_ = nullptr;
  UnitPaHubManager* unitPaHub_ = nullptr;
  LiveFeedsManager* liveFeeds_ = nullptr;
  class GoogleCalendarClient* googleCalendar_ = nullptr;
  ConfigManager* configManager_ = nullptr;

  String pendingLocalAction_;
  uint32_t pendingLocalActionTurnId_ = 0;
  uint32_t pendingLocalActionMs_ = 0;
  bool pendingLocalActionValid_ = false;

  LocalStatus handleLocal(const String& name, const JsonVariantConst& args, JsonDocument& response);
  String errorJson(const String& code, const String& message);
};
