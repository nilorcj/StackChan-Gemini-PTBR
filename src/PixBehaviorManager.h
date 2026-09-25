#pragma once

#include <Arduino.h>

enum class BehaviorEvent : uint8_t {
  PRESENCE,
  TOUCH,
  IDLE_TICK
};

enum class BehaviorDecision : uint8_t {
  NO_ACTION,
  LOOK_AT_USER,
  HAPPY_SHORT,
  CURIOUS_TILT,
  MEIGO_SHORT,
  IDLE_BREATH
};

class PixBehaviorManager {
 public:
  PixBehaviorManager();

  // Primary event evaluator - returns decision without directly accessing hardware
  BehaviorDecision onEvent(BehaviorEvent event, uint32_t nowMs);

  // Background passive state decay / homeostasis
  void update(uint32_t nowMs);

  // Telemetry getters
  uint8_t energy() const { return energy_; }
  uint8_t curiosity() const { return curiosity_; }
  uint8_t affection() const { return affection_; }

  static const char* decisionToString(BehaviorDecision d);
  static const char* eventToString(BehaviorEvent e);

 private:
  uint8_t energy_ = 60;
  uint8_t curiosity_ = 55;
  uint8_t affection_ = 50;

  uint32_t lastPresenceReactionMs_ = 0;
  uint32_t presenceCooldownMs_ = 12000;

  uint32_t lastTouchReactionMs_ = 0;
  uint32_t touchCooldownMs_ = 3000;

  uint32_t lastIdleTickMs_ = 0;
  uint32_t nextIdleIntervalMs_ = 10000;
  uint32_t lastSpontaneousActionMs_ = 0;
  uint32_t spontaneousCooldownMs_ = 22000;

  uint32_t lastDecayMs_ = 0;

  BehaviorDecision decidePresence(uint32_t nowMs);
  BehaviorDecision decideTouch(uint32_t nowMs);
  BehaviorDecision decideIdle(uint32_t nowMs);

  void logDecision(const char* eventStr, BehaviorDecision decision);
};
