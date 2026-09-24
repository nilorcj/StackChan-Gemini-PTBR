#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class UnitPaHubManager;
class LiveFeedsManager;

// Non-blocking robot-side emotion/effect preset controller.
// Gemini should call high-level emotion names; this controller owns smooth LED effects.
class EmotionController {
 public:
  void begin();
  void setUnitPaHub(UnitPaHubManager* hub) { unitPaHub_ = hub; }
  void setLiveFeeds(const LiveFeedsManager* feeds) { liveFeeds_ = feeds; }
  void setLifted(bool lifted) {
    if (isLifted_ != lifted) {
      isLifted_ = lifted;
      if (isLifted_) resetRestHumanization();
      forceFullRedraw_ = true;
      needsFaceRedraw_ = true;
    }
  }
  bool isLifted() const { return isLifted_; }
  void setShaken(bool shaken) {
    if (isShaken_ != shaken) {
      isShaken_ = shaken;
      if (isShaken_) resetRestHumanization();
      forceFullRedraw_ = true;
      needsFaceRedraw_ = true;
    }
  }
  bool isShaken() const { return isShaken_; }
  void setProlongedPetting(bool active) {
    if (isProlongedPetting_ != active) {
      isProlongedPetting_ = active;
      if (isProlongedPetting_) resetRestHumanization();
      forceFullRedraw_ = true;
      needsFaceRedraw_ = true;
    }
  }
  bool isProlongedPetting() const { return isProlongedPetting_; }
  void setDoNotDisturb(bool dnd) {
    if (isDnd_ != dnd) {
      isDnd_ = dnd;
      if (isDnd_) resetRestHumanization();
      forceFullRedraw_ = true;
      needsFaceRedraw_ = true;
    }
  }
  bool isDoNotDisturb() const { return isDnd_; }
  void setMicActive(bool active) {
    if (micActive_ != active) {
      micActive_ = active;
      if (micActive_) resetRestHumanization();
      forceFullRedraw_ = true;
      needsFaceRedraw_ = true;
    }
  }
  bool isMicActive() const { return micActive_; }
  void setIsSpeaking(bool speaking);
  bool isSpeaking() const { return isSpeaking_; }
  void setPhysicalMute(bool mute) {
    if (physicalMute_ != mute) {
      physicalMute_ = mute;
      forceFullRedraw_ = true;
      needsFaceRedraw_ = true;
    }
  }
  bool isPhysicalMute() const { return physicalMute_; }

  void setConversationArmed(bool armed) {
    if (conversationArmed_ != armed) {
      conversationArmed_ = armed;
      if (conversationArmed_) resetRestHumanization();
    }
  }
  bool isConversationArmed() const { return conversationArmed_; }

  void setTurnStateIdle(bool idle) {
    if (turnStateIdle_ != idle) {
      turnStateIdle_ = idle;
      if (!turnStateIdle_) resetRestHumanization();
    }
  }
  bool isTurnStateIdle() const { return turnStateIdle_; }

  void showQuickStatus(uint32_t durationMs = 4000) {
    quickStatusUntilMs_ = millis() + durationMs;
    forceFullRedraw_ = true;
    needsFaceRedraw_ = true;
    lastDrawerSnapshot_ = DrawerSnapshot();
  }
  bool isQuickStatusActive() const { return millis() < quickStatusUntilMs_; }
  void hideQuickStatus() {
    quickStatusUntilMs_ = 0;
    forceFullRedraw_ = true;
    needsFaceRedraw_ = true;
  }
  void setQuickStatusData(int rssi, const String& ip, uint8_t batPct, float batVolts,
                          bool charging, uint8_t iaState, float temp, float hum, int tofMm = -1) {
    wifiRssi_ = rssi;
    ipAddress_ = ip;
    batteryPct_ = batPct;
    batteryVolts_ = batVolts;
    batteryCharging_ = charging;
    iaStatus_ = iaState;
    currentTemp_ = temp;
    currentHumidity_ = hum;
    currentTofMm_ = tofMm;
  }

  bool setEmotion(const String& emotion);
  const String& currentEmotion() const { return current_; }
  void loop();

 private:
  enum class Mode {
    Neutral,
    Listening,
    Speaking,
    Thinking,
    Looking,
    Happy,
    Angry,
    Found,
    Error,
    Sleep,
    Meigo,
    Scared,
    Surprised,
  };

  Mode mode_ = Mode::Neutral;
  String current_ = "neutral";
  uint32_t lastFrameMs_ = 0;
  uint32_t lastFaceMs_ = 0;
  uint32_t sleepStartedMs_ = 0;
  uint16_t frame_ = 0;
  bool sleepDisplayOff_ = false;
  bool needsFaceRedraw_ = false;
  uint8_t savedDisplayBrightness_ = 127;
  UnitPaHubManager* unitPaHub_ = nullptr;
  const LiveFeedsManager* liveFeeds_ = nullptr;
  bool isLifted_ = false;
  bool isShaken_ = false;
  bool isProlongedPetting_ = false;
  bool isDnd_ = false;
  uint32_t quickStatusUntilMs_ = 0;
  int wifiRssi_ = -60;
  String ipAddress_ = "";
  uint8_t batteryPct_ = 100;
  float batteryVolts_ = 4.15f;
  bool batteryCharging_ = false;
  bool geminiConnected_ = false;
  uint8_t iaStatus_ = 0; // 0=OFF, 1=..., 2=ON
  float currentTemp_ = 0.0f;
  float currentHumidity_ = 0.0f;
  int currentTofMm_ = -1;
  bool isSpeaking_ = false;
  bool micActive_ = false;
  bool physicalMute_ = false;

  struct DrawerSnapshot {
    int rssi = -999;
    String ip = "";
    uint8_t batPct = 255;
    uint8_t iaStatus = 255;
    float temp = -999.0f;
    float hum = -999.0f;
    int tofMm = -999;
    uint32_t lastTofRenderMs = 0;
  };
  DrawerSnapshot lastDrawerSnapshot_;
  bool quickStatusDataChanged();

  // Partial face rendering sprite geometry constants (strictly non-overlapping)
  static constexpr int kMouthSpriteX = 110;
  static constexpr int kMouthSpriteY = 144;
  static constexpr int kMouthSpriteW = 100;
  static constexpr int kMouthSpriteH = 54;

  static constexpr int kLeftEyeSpriteX = 54;
  static constexpr int kLeftEyeSpriteY = 56;
  static constexpr int kLeftEyeSpriteW = 96;
  static constexpr int kLeftEyeSpriteH = 80;

  static constexpr int kRightEyeSpriteX = 170;
  static constexpr int kRightEyeSpriteY = 56;
  static constexpr int kRightEyeSpriteW = 96;
  static constexpr int kRightEyeSpriteH = 80;

  struct FaceState {
    uint16_t irisColor = 0x3DFE;
    uint16_t glowAccent = 0x037A;
    uint16_t blushColor = 0xF9F2;
    int pupilDx = 0;
    int pupilDy = 0;
    bool closed = false;
    bool isAngry = false;
    bool isHappy = false;
    bool isMeigo = false;
    bool isScared = false;
    bool isSurprised = false;
    bool isSpeaking = false;
    bool isThinking = false;
    bool isLooking = false;
    bool isSleep = false;
    uint8_t talkShape = 0;
    int talkYOffset = 0;
  };

  // Dedicated partial update sprites allocated once in internal SRAM
  M5Canvas mouthSprite_;
  M5Canvas leftEyeSprite_;
  M5Canvas rightEyeSprite_;
  bool partialRenderingEnabled_ = false;
  bool forceFullRedraw_ = true;

  // Tracking state for dirty regions & full-redraw triggers
  Mode lastRenderedMode_ = Mode::Neutral;
  bool lastRenderedShaken_ = false;
  bool lastRenderedLifted_ = false;
  bool lastRenderedQuickStatus_ = false;
  bool lastRenderedMicActive_ = false;
  bool lastRenderedPhysicalMute_ = false;
  bool lastRenderedSpeaking_ = false;
  int lastPupilDx_ = 999;
  int lastPupilDy_ = 999;
  bool lastClosed_ = false;
  int lastTalkShape_ = -1;
  int lastTalkYOffset_ = -1;

  static Mode parseMode(const String& emotion, String& normalized);
  static void hsvToRgb(uint8_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b);
  static uint8_t wave8(uint16_t x, uint8_t minValue, uint8_t maxValue);
  static uint8_t scale8(uint8_t value, uint8_t scale);
  static bool externalPowerPresent();
  bool sleepVisualOffDue() const;
  void enterSleepDisplayOff(bool externalPower);
  void restoreSleepDisplay();
  void setLed(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
  void setAll(uint8_t r, uint8_t g, uint8_t b);
  void refresh();
  void drawLabel();
  void drawQuickStatusDrawer(M5Canvas& d);

  // Rest humanization state machine (Mode::Neutral, IDLE, disarmed)
  enum class BlinkState : uint8_t {
    OPEN = 0,
    FIRST_CLOSED,
    DOUBLE_WAIT,
    SECOND_CLOSED
  };
  BlinkState blinkState_ = BlinkState::OPEN;
  uint32_t nextBlinkDeadlineMs_ = 0;
  uint32_t blinkStageDeadlineMs_ = 0;
  bool isBlinking_ = false;

  float currentPupilDx_ = 0.0f;
  float currentPupilDy_ = 0.0f;
  int targetPupilDx_ = 0;
  int targetPupilDy_ = 0;
  uint32_t nextPupilDriftMs_ = 0;

  bool conversationArmed_ = false;
  bool turnStateIdle_ = true;

  void updateBlinkState(uint32_t now);
  void updatePupilDrift(uint32_t now);
  void resetRestHumanization();

  FaceState computeFaceState() const;
  void renderFace();
  void renderFullFace(LovyanGFX& display, const FaceState& s);
  void renderPartialFace(LovyanGFX& display, const FaceState& s);
  void drawEye(LovyanGFX& d, int ex, int eyCenter, bool leftSide, const FaceState& s);
  void drawMouth(LovyanGFX& d, int cx, int mouthY, const FaceState& s);

  void renderNeutral();
  void renderListening();
  void renderSpeaking();
  void renderThinking();
  void renderLooking();
  void renderHappy();
  void renderAngry();
  void renderFound();
  void renderError();
  void renderSleep();
  void renderMeigo();
  void renderScared();
  void renderSurprised();
};
