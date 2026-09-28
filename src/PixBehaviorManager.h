#pragma once

#include <Arduino.h>
#include <math.h>

enum class BehaviorEvent : uint8_t {
  PRESENCE,
  TOUCH,
  IDLE_TICK,
  WAVE,
  CONVERSATION_TURN_END,
  LIFT_RETURN
};

enum class BehaviorDecision : uint8_t {
  NO_ACTION,
  LOOK_AT_USER,
  HAPPY_SHORT,
  CURIOUS_TILT,
  MEIGO_SHORT,
  IDLE_BREATH,
  SOCIAL_MICRO_ADJUST
};

class PixBehaviorManager {
 public:
  PixBehaviorManager();

  // Primary event evaluator - returns decision without directly accessing hardware
  BehaviorDecision onEvent(BehaviorEvent event, uint32_t nowMs);

  // Background passive state decay / homeostasis
  void update(uint32_t nowMs);

  // Telemetry getters (persistent base states)
  uint8_t energy() const { return energy_; }
  uint8_t curiosity() const { return curiosity_; }
  uint8_t affection() const { return affection_; }

  // Etapa 6: Contexto temporal leve (offsets transitivos sem sobrescrever os estados base)
  int8_t temporalEnergyBias() const { return temporalEnergyBias_; }
  int8_t temporalCuriosityBias() const { return temporalCuriosityBias_; }

  // Etapa 7: Coerência emocional de curto prazo ("afterglow" / histerese de 1 a 5 min)
  int8_t afterglowEnergyBias() const { return static_cast<int8_t>(roundf(afterglowEnergy_)); }
  int8_t afterglowCuriosityBias() const { return static_cast<int8_t>(roundf(afterglowCuriosity_)); }
  int8_t afterglowAffectionBias() const { return static_cast<int8_t>(roundf(afterglowAffection_)); }

  uint8_t effectiveEnergy() const {
    int val = static_cast<int>(energy_) + temporalEnergyBias_ + afterglowEnergyBias();
    if (val < 0) return 0;
    if (val > 100) return 100;
    return static_cast<uint8_t>(val);
  }

  uint8_t effectiveCuriosity() const {
    int val = static_cast<int>(curiosity_) + temporalCuriosityBias_ + afterglowCuriosityBias();
    if (val < 0) return 0;
    if (val > 100) return 100;
    return static_cast<uint8_t>(val);
  }

  uint8_t effectiveAffection() const {
    int val = static_cast<int>(affection_) + afterglowAffectionBias();
    if (val < 0) return 0;
    if (val > 100) return 100;
    return static_cast<uint8_t>(val);
  }

  void setSimulatedHour(int8_t h) {
    simulatedHour_ = h;
    computeTemporalBiases();
  }
  int8_t simulatedHour() const { return simulatedHour_; }

  // Etapa 8: Atenção social em presença estável ou recente
  void setUserPresent(bool present) { userPresent_ = present; }
  bool isUserPresent() const { return userPresent_; }
  bool isSocialAttentionActive(uint32_t nowMs) const;

  // Etapa 9: Coerência contextual pós-interação
  BehaviorDecision lastAction() const { return lastAction_; }
  uint32_t lastConversationEndMs() const { return lastConversationEndMs_; }
  uint32_t lastTouchMs() const { return lastTouchMs_; }
  uint32_t lastWaveMs() const { return lastWaveMs_; }
  uint32_t lastLiftReturnMs() const { return lastLiftReturnMs_; }

  static const char* decisionToString(BehaviorDecision d);
  static const char* eventToString(BehaviorEvent e);

 private:
  uint8_t energy_ = 60;
  uint8_t curiosity_ = 55;
  uint8_t affection_ = 50;

  int8_t simulatedHour_ = -1;

  int8_t temporalEnergyBias_ = 0;
  int8_t temporalCuriosityBias_ = 0;
  uint32_t lastTemporalUpdateMs_ = 0;

  float afterglowEnergy_ = 0.0f;
  float afterglowCuriosity_ = 0.0f;
  float afterglowAffection_ = 0.0f;
  uint32_t lastAfterglowUpdateMs_ = 0;

  uint32_t lastInteractionMs_ = 0;

  // Etapa 9: Histórico contextual de interações recentes
  uint32_t lastConversationEndMs_ = 0;
  uint32_t lastTouchMs_ = 0;
  uint32_t lastWaveMs_ = 0;
  uint32_t lastLiftReturnMs_ = 0;
  BehaviorDecision lastAction_ = BehaviorDecision::NO_ACTION;

  bool userPresent_ = false;
  uint32_t lastSocialReorientMs_ = 0;
  uint32_t socialReorientCooldownMs_ = 45000;

  uint32_t lastPresenceReactionMs_ = 0;
  uint32_t presenceCooldownMs_ = 12000;

  uint32_t lastTouchReactionMs_ = 0;
  uint32_t touchCooldownMs_ = 2500;

  uint32_t lastIdleTickMs_ = 0;
  uint32_t nextIdleIntervalMs_ = 10000;
  uint32_t lastSpontaneousActionMs_ = 0;
  uint32_t spontaneousCooldownMs_ = 22000;

  uint32_t lastDecayMs_ = 0;

  void computeTemporalBiases();

  BehaviorDecision decidePresence(uint32_t nowMs);
  BehaviorDecision decideTouch(uint32_t nowMs);
  BehaviorDecision decideIdle(uint32_t nowMs);
  BehaviorDecision decideWave(uint32_t nowMs);
  BehaviorDecision decideConversationEnd(uint32_t nowMs);

  void logDecision(const char* eventStr, BehaviorDecision decision);
  void logState(const char* eventTag);
};
