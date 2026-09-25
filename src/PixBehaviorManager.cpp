#include "PixBehaviorManager.h"
#include <esp_random.h>

PixBehaviorManager::PixBehaviorManager()
    : energy_(60), curiosity_(55), affection_(50),
      lastPresenceReactionMs_(0), presenceCooldownMs_(12000),
      lastTouchReactionMs_(0), touchCooldownMs_(2000),
      lastIdleTickMs_(0), nextIdleIntervalMs_(10000),
      lastSpontaneousActionMs_(0), spontaneousCooldownMs_(22000),
      lastDecayMs_(0) {}

const char* PixBehaviorManager::decisionToString(BehaviorDecision d) {
  switch (d) {
    case BehaviorDecision::NO_ACTION:     return "NO_ACTION";
    case BehaviorDecision::LOOK_AT_USER:  return "LOOK_AT_USER";
    case BehaviorDecision::HAPPY_SHORT:   return "HAPPY_SHORT";
    case BehaviorDecision::CURIOUS_TILT:  return "CURIOUS_TILT";
    case BehaviorDecision::MEIGO_SHORT:   return "MEIGO_SHORT";
    case BehaviorDecision::IDLE_BREATH:   return "IDLE_BREATH";
    default:                              return "UNKNOWN";
  }
}

const char* PixBehaviorManager::eventToString(BehaviorEvent e) {
  switch (e) {
    case BehaviorEvent::PRESENCE:  return "PRESENCE";
    case BehaviorEvent::TOUCH:     return "TOUCH";
    case BehaviorEvent::IDLE_TICK: return "IDLE";
    default:                       return "UNKNOWN";
  }
}

void PixBehaviorManager::logDecision(const char* eventStr, BehaviorDecision decision) {
  Serial.printf("[BEHAVIOR] event=%s energy=%u curiosity=%u affection=%u decision=%s\n",
                eventStr, energy_, curiosity_, affection_, decisionToString(decision));
}

BehaviorDecision PixBehaviorManager::onEvent(BehaviorEvent event, uint32_t nowMs) {
  switch (event) {
    case BehaviorEvent::PRESENCE:
      return decidePresence(nowMs);
    case BehaviorEvent::TOUCH:
      return decideTouch(nowMs);
    case BehaviorEvent::IDLE_TICK:
      return decideIdle(nowMs);
    default:
      return BehaviorDecision::NO_ACTION;
  }
}

BehaviorDecision PixBehaviorManager::decidePresence(uint32_t nowMs) {
  if (nowMs - lastPresenceReactionMs_ < presenceCooldownMs_) {
    return BehaviorDecision::NO_ACTION;
  }

  // Ajustes de estado sutis por evento de chegada
  if (curiosity_ <= 97) curiosity_ += 3; else curiosity_ = 100;
  if (affection_ <= 98) affection_ += 2; else affection_ = 100;
  if (energy_ <= 98) energy_ += 2; else energy_ = 100;

  // Distribuicao moderada: 50% NO_ACTION (sem carencia), 22% LOOK_AT_USER, 16% CURIOUS_TILT, 12% HAPPY_SHORT
  uint32_t roll = esp_random() % 100;
  BehaviorDecision decision = BehaviorDecision::NO_ACTION;

  if (roll < 50) {
    decision = BehaviorDecision::NO_ACTION;
    lastPresenceReactionMs_ = nowMs;
    presenceCooldownMs_ = 6000 + (esp_random() % 4001); // 6-10s antes da proxima avaliacao se ignorada
    logDecision("PRESENCE", decision);
    return BehaviorDecision::NO_ACTION;
  } else if (roll < 72) {
    decision = BehaviorDecision::LOOK_AT_USER; // 22%
  } else if (roll < 88) {
    decision = BehaviorDecision::CURIOUS_TILT; // 16%
  } else {
    decision = BehaviorDecision::HAPPY_SHORT;  // 12% (sorriso sutil)
  }

  lastPresenceReactionMs_ = nowMs;
  presenceCooldownMs_ = 12000 + (esp_random() % 6001); // 12-18s cooldown apos reagir

  logDecision("PRESENCE", decision);
  return decision;
}

BehaviorDecision PixBehaviorManager::decideTouch(uint32_t nowMs) {
  if (nowMs - lastTouchReactionMs_ < touchCooldownMs_) {
    return BehaviorDecision::NO_ACTION;
  }

  // Afago eleva afeto e energia de forma sutil
  if (affection_ <= 94) affection_ += 6; else affection_ = 100;
  if (energy_ <= 97) energy_ += 3; else energy_ = 100;

  lastTouchReactionMs_ = nowMs;
  touchCooldownMs_ = 2500;

  return BehaviorDecision::MEIGO_SHORT;
}

BehaviorDecision PixBehaviorManager::decideIdle(uint32_t nowMs) {
  if (nowMs - lastIdleTickMs_ < nextIdleIntervalMs_) {
    return BehaviorDecision::NO_ACTION;
  }

  lastIdleTickMs_ = nowMs;
  nextIdleIntervalMs_ = 9000 + (esp_random() % 8001); // Intervalo organico variavel entre 9s e 17s

  // Acoes espontaneas raras: apenas se cooldown de 18-30s tiver expirado
  bool canSpontaneous = (nowMs - lastSpontaneousActionMs_ >= spontaneousCooldownMs_);
  if (canSpontaneous) {
    uint32_t rollSpontaneous = esp_random() % 100;
    if (rollSpontaneous < 22) { // 22% de chance quando elegivel -> raro e organico
      lastSpontaneousActionMs_ = nowMs;
      spontaneousCooldownMs_ = 18000 + (esp_random() % 12001); // Cooldown rigoroso de 18-30s

      BehaviorDecision decision = (esp_random() % 2 == 0) ? BehaviorDecision::LOOK_AT_USER : BehaviorDecision::CURIOUS_TILT;
      logDecision("IDLE_SPONTANEOUS", decision);
      return decision;
    }
  }

  // Comportamento normal de repouso: alternancia entre respiracao suave e repouso silencioso
  uint32_t roll = esp_random() % 100;
  if (roll < 45) {
    return BehaviorDecision::IDLE_BREATH; // 45% respiracao ociosa
  }

  return BehaviorDecision::NO_ACTION; // 55% silencio/repouso calmo
}

void PixBehaviorManager::update(uint32_t nowMs) {
  // Gradual decay / homeostasis every 15 seconds
  if (lastDecayMs_ == 0) {
    lastDecayMs_ = nowMs;
    return;
  }

  if (nowMs - lastDecayMs_ >= 15000) {
    lastDecayMs_ = nowMs;
    if (energy_ > 20) energy_--;
    if (curiosity_ > 50) curiosity_--;
    else if (curiosity_ < 50) curiosity_++;
    if (affection_ > 40) affection_--;
  }
}
