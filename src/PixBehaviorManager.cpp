#include "PixBehaviorManager.h"
#include <esp_random.h>

PixBehaviorManager::PixBehaviorManager()
    : energy_(60), curiosity_(55), affection_(50),
      lastPresenceReactionMs_(0), presenceCooldownMs_(8000),
      lastTouchReactionMs_(0), touchCooldownMs_(2000),
      lastIdleSpontaneousMs_(0), idleCooldownMs_(7500),
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

  // Internal state updates on presence perception
  if (curiosity_ <= 95) curiosity_ += 5; else curiosity_ = 100;
  if (affection_ <= 98) affection_ += 2; else affection_ = 100;
  if (energy_ <= 98) energy_ += 2; else energy_ = 100;

  // Moderate response distribution: ~45% NO_ACTION, ~55% reaction
  uint32_t roll = esp_random() % 100;
  BehaviorDecision decision = BehaviorDecision::NO_ACTION;

  if (roll < 45) {
    decision = BehaviorDecision::NO_ACTION;
    // Set a moderate re-check backoff so we don't roll every single loop
    lastPresenceReactionMs_ = nowMs;
    presenceCooldownMs_ = 4000 + (esp_random() % 3001); // 4-7s before next evaluation if ignored
    return BehaviorDecision::NO_ACTION;
  } else if (roll < 70) {
    decision = BehaviorDecision::LOOK_AT_USER; // 25% total
  } else if (roll < 85) {
    decision = BehaviorDecision::CURIOUS_TILT; // 15% total
  } else {
    decision = BehaviorDecision::HAPPY_SHORT;  // 15% total
  }

  lastPresenceReactionMs_ = nowMs;
  presenceCooldownMs_ = 8000 + (esp_random() % 4001); // 8-12s cooldown after reaction

  logDecision("PRESENCE", decision);
  return decision;
}

BehaviorDecision PixBehaviorManager::decideTouch(uint32_t nowMs) {
  if (nowMs - lastTouchReactionMs_ < touchCooldownMs_) {
    return BehaviorDecision::NO_ACTION;
  }

  // Touch substantially boosts affection and gently raises energy
  if (affection_ <= 92) affection_ += 8; else affection_ = 100;
  if (energy_ <= 96) energy_ += 4; else energy_ = 100;
  if (curiosity_ > 30) curiosity_ -= 2;

  // Deterministic + randomized distribution:
  // Meigo (affectionate purr) 50%, Happy 30%, Curious tilt 20%
  uint32_t roll = esp_random() % 100;
  BehaviorDecision decision = BehaviorDecision::MEIGO_SHORT;

  if (roll < 50) {
    decision = BehaviorDecision::MEIGO_SHORT;
  } else if (roll < 80) {
    decision = BehaviorDecision::HAPPY_SHORT;
  } else {
    decision = BehaviorDecision::CURIOUS_TILT;
  }

  lastTouchReactionMs_ = nowMs;
  touchCooldownMs_ = 2000 + (esp_random() % 2001); // 2-4s cooldown

  logDecision("TOUCH", decision);
  return decision;
}

BehaviorDecision PixBehaviorManager::decideIdle(uint32_t nowMs) {
  if (nowMs - lastIdleSpontaneousMs_ < idleCooldownMs_) {
    return BehaviorDecision::NO_ACTION;
  }

  // Idle distribution:
  // 50% IDLE_BREATH (preserves breathing simulation)
  // 20% NO_ACTION (calm resting)
  // 15% LOOK_AT_USER (spontaneous glance)
  // 15% CURIOUS_TILT (spontaneous curiosity)
  uint32_t roll = esp_random() % 100;
  BehaviorDecision decision = BehaviorDecision::IDLE_BREATH;

  if (roll < 50) {
    decision = BehaviorDecision::IDLE_BREATH;
    lastIdleSpontaneousMs_ = nowMs;
    idleCooldownMs_ = 7000 + (esp_random() % 2001); // 7-9s between breaths
    // No flood logging for standard breathing ticks
  } else if (roll < 70) {
    decision = BehaviorDecision::NO_ACTION;
    lastIdleSpontaneousMs_ = nowMs;
    idleCooldownMs_ = 5000 + (esp_random() % 3001); // 5-8s pause
  } else if (roll < 85) {
    decision = BehaviorDecision::LOOK_AT_USER;
    lastIdleSpontaneousMs_ = nowMs;
    idleCooldownMs_ = 18000 + (esp_random() % 12001); // 18-30s spontaneous cooldown
    logDecision("IDLE", decision);
  } else {
    decision = BehaviorDecision::CURIOUS_TILT;
    lastIdleSpontaneousMs_ = nowMs;
    idleCooldownMs_ = 18000 + (esp_random() % 12001); // 18-30s spontaneous cooldown
    logDecision("IDLE", decision);
  }

  return decision;
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
