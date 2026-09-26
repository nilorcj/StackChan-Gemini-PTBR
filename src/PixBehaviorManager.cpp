#include "PixBehaviorManager.h"
#include <esp_random.h>

PixBehaviorManager::PixBehaviorManager()
    : energy_(60), curiosity_(55), affection_(50),
      lastInteractionMs_(0),
      lastPresenceReactionMs_(0), presenceCooldownMs_(12000),
      lastTouchReactionMs_(0), touchCooldownMs_(2500),
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
    case BehaviorEvent::PRESENCE:              return "PRESENCE";
    case BehaviorEvent::TOUCH:                 return "TOUCH";
    case BehaviorEvent::IDLE_TICK:             return "IDLE";
    case BehaviorEvent::WAVE:                  return "WAVE";
    case BehaviorEvent::CONVERSATION_TURN_END: return "CONVERSATION";
    default:                                   return "UNKNOWN";
  }
}

void PixBehaviorManager::logDecision(const char* eventStr, BehaviorDecision decision) {
  Serial.printf("[BEHAVIOR] event=%s energy=%u curiosity=%u affection=%u decision=%s\n",
                eventStr, energy_, curiosity_, affection_, decisionToString(decision));
}

void PixBehaviorManager::logState(const char* eventTag) {
  Serial.printf("[BEHAVIOR:STATE] event=%s energy=%u curiosity=%u affection=%u\n",
                eventTag, energy_, curiosity_, affection_);
}

BehaviorDecision PixBehaviorManager::onEvent(BehaviorEvent event, uint32_t nowMs) {
  switch (event) {
    case BehaviorEvent::PRESENCE:
      return decidePresence(nowMs);
    case BehaviorEvent::TOUCH:
      return decideTouch(nowMs);
    case BehaviorEvent::IDLE_TICK:
      return decideIdle(nowMs);
    case BehaviorEvent::WAVE:
      return decideWave(nowMs);
    case BehaviorEvent::CONVERSATION_TURN_END:
      return decideConversationEnd(nowMs);
    default:
      return BehaviorDecision::NO_ACTION;
  }
}

BehaviorDecision PixBehaviorManager::decidePresence(uint32_t nowMs) {
  if (nowMs - lastPresenceReactionMs_ < presenceCooldownMs_) {
    return BehaviorDecision::NO_ACTION;
  }

  // Presença de chegada: pequeno aumento de curiosity (+2, max 100)
  if (curiosity_ <= 98) curiosity_ += 2; else curiosity_ = 100;

  // Pequeno aumento de affection (+2) se já houver histórico recente de interação (últimos 2 min)
  bool hadRecentInteraction = (lastInteractionMs_ > 0 && (nowMs - lastInteractionMs_ < 120000));
  if (hadRecentInteraction) {
    if (affection_ <= 98) affection_ += 2; else affection_ = 100;
  }

  // Influência de energia no limiar de iniciativa (50% base, variando de 35% a 70%)
  // Menor energia -> maior probabilidade de NO_ACTION (menos iniciativa)
  int noActionThreshold = 50 + (50 - static_cast<int>(energy_)) * 4 / 10;
  if (noActionThreshold < 35) noActionThreshold = 35;
  if (noActionThreshold > 70) noActionThreshold = 70;

  uint32_t roll = esp_random() % 100;
  BehaviorDecision decision = BehaviorDecision::NO_ACTION;

  if (roll < static_cast<uint32_t>(noActionThreshold)) {
    decision = BehaviorDecision::NO_ACTION;
    lastPresenceReactionMs_ = nowMs;
    presenceCooldownMs_ = 6000 + (esp_random() % 4001); // 6-10s antes da próxima avaliação se ignorada
    logDecision("PRESENCE", decision);
    return BehaviorDecision::NO_ACTION;
  }

  // Pesos dinâmicos com base nos estados internos
  // Base: Look=35, Tilt=25, Happy=15
  int wLook = 35;
  int wTilt = 25 + ((curiosity_ > 50) ? (curiosity_ - 50) * 5 / 10 : 0);
  int wHappy = 15 + ((affection_ > 50) ? (affection_ - 50) * 6 / 10 : 0);

  // Baixa energia favorece gestos discretos (LOOK_AT_USER) sobre animações expansivas
  if (energy_ < 40) {
    wLook += 15;
    wTilt = (wTilt * 7) / 10;
    wHappy = (wHappy * 5) / 10;
  }

  int totalWeight = wLook + wTilt + wHappy;
  if (totalWeight <= 0) totalWeight = 1;
  uint32_t actionRoll = esp_random() % totalWeight;

  if (actionRoll < static_cast<uint32_t>(wLook)) {
    decision = BehaviorDecision::LOOK_AT_USER;
  } else if (actionRoll < static_cast<uint32_t>(wLook + wTilt)) {
    decision = BehaviorDecision::CURIOUS_TILT;
  } else {
    decision = BehaviorDecision::HAPPY_SHORT;
  }

  // Reação física consome pequena quantidade de energia (-1)
  if (energy_ > 10) energy_ -= 1;

  lastPresenceReactionMs_ = nowMs;
  presenceCooldownMs_ = 12000 + (esp_random() % 6001); // 12-18s cooldown após reagir

  logState("PRESENCE");
  logDecision("PRESENCE", decision);
  return decision;
}

BehaviorDecision PixBehaviorManager::decideTouch(uint32_t nowMs) {
  if (nowMs - lastTouchReactionMs_ < touchCooldownMs_) {
    return BehaviorDecision::NO_ACTION;
  }

  lastInteractionMs_ = nowMs;
  lastTouchReactionMs_ = nowMs;
  touchCooldownMs_ = 2500;

  // Afago / interação carinhosa:
  // - aumenta affection de forma perceptível, mas moderada (+4, max 100)
  // - pequeno aumento de curiosity (+2, max 100)
  // - toque afetuoso reconforta levemente (+1 energy se < 80)
  if (affection_ <= 96) affection_ += 4; else affection_ = 100;
  if (curiosity_ <= 98) curiosity_ += 2; else curiosity_ = 100;
  if (energy_ < 80) energy_ += 1;

  BehaviorDecision decision = BehaviorDecision::MEIGO_SHORT;
  logState("PET");
  logDecision("TOUCH", decision);
  return decision;
}

BehaviorDecision PixBehaviorManager::decideWave(uint32_t nowMs) {
  lastInteractionMs_ = nowMs;
  // HAND_WAVE: pode aumentar levemente curiosity (+2, max 100)
  if (curiosity_ <= 98) curiosity_ += 2; else curiosity_ = 100;
  // Custo muito leve de atenção (-1 energy se > 15)
  if (energy_ > 15) energy_ -= 1;

  logState("WAVE");
  return BehaviorDecision::NO_ACTION;
}

BehaviorDecision PixBehaviorManager::decideConversationEnd(uint32_t nowMs) {
  lastInteractionMs_ = nowMs;
  // Turno de conversa concluído:
  // - pequeno aumento de affection (+2, max 100)
  // - curiosidade satisfeita: reduz suavemente curiosity (-2, min 30)
  // - pequeno consumo de energia pela interação vocal ativa (-1 energy se > 10)
  if (affection_ <= 98) affection_ += 2; else affection_ = 100;
  if (curiosity_ >= 32) curiosity_ -= 2; else curiosity_ = 30;
  if (energy_ > 10) energy_ -= 1;

  logState("CONVERSATION");
  return BehaviorDecision::NO_ACTION;
}

BehaviorDecision PixBehaviorManager::decideIdle(uint32_t nowMs) {
  if (nowMs - lastIdleTickMs_ < nextIdleIntervalMs_) {
    return BehaviorDecision::NO_ACTION;
  }

  lastIdleTickMs_ = nowMs;
  nextIdleIntervalMs_ = 9000 + (esp_random() % 8001); // Intervalo orgânico variável entre 9s e 17s

  // Ações espontâneas raras: apenas se cooldown de 18-30s tiver expirado
  bool canSpontaneous = (nowMs - lastSpontaneousActionMs_ >= spontaneousCooldownMs_);
  if (canSpontaneous) {
    // Probabilidade modulada sutilmente pela energia (entre 8% e 28%, base 22%)
    int spontaneousChance = 22 + (static_cast<int>(energy_) - 50) * 15 / 100;
    if (spontaneousChance < 8) spontaneousChance = 8;
    if (spontaneousChance > 28) spontaneousChance = 28;

    uint32_t rollSpontaneous = esp_random() % 100;
    if (rollSpontaneous < static_cast<uint32_t>(spontaneousChance)) {
      lastSpontaneousActionMs_ = nowMs;
      spontaneousCooldownMs_ = 18000 + (esp_random() % 12001); // Cooldown rigoroso de 18-30s preservado

      // Ação espontânea consome 1 ponto de energia
      if (energy_ > 10) energy_ -= 1;

      BehaviorDecision decision = BehaviorDecision::LOOK_AT_USER;
      // Alta afeição dá chance de um sorriso sutil espontâneo
      if (affection_ >= 65 && (esp_random() % 100) < 35) {
        decision = BehaviorDecision::HAPPY_SHORT;
      } else if (curiosity_ >= 60) {
        // Alta curiosidade favorece olhar ou inclinação curiosa
        decision = (esp_random() % 100 < 65) ? BehaviorDecision::CURIOUS_TILT : BehaviorDecision::LOOK_AT_USER;
      } else {
        decision = (esp_random() % 2 == 0) ? BehaviorDecision::LOOK_AT_USER : BehaviorDecision::CURIOUS_TILT;
      }

      logState("IDLE_SPONTANEOUS");
      logDecision("IDLE_SPONTANEOUS", decision);
      return decision;
    }
  }

  // Comportamento normal de repouso: alternância entre respiração suave e repouso silencioso
  uint32_t roll = esp_random() % 100;
  if (roll < 45) {
    return BehaviorDecision::IDLE_BREATH; // 45% respiração ociosa
  }

  return BehaviorDecision::NO_ACTION; // 55% silêncio/repouso calmo
}

void PixBehaviorManager::update(uint32_t nowMs) {
  // Gradual decay / homeostasis every 20 seconds
  if (lastDecayMs_ == 0) {
    lastDecayMs_ = nowMs;
    return;
  }

  if (nowMs - lastDecayMs_ >= 20000) {
    lastDecayMs_ = nowMs;

    // 1. Idle prolongado: recuperar energia lentamente até baseline descansado (60)
    // Se não houve interação nos últimos 30s
    bool isIdleResting = (lastInteractionMs_ == 0 || (nowMs - lastInteractionMs_ > 30000));
    if (isIdleResting) {
      if (energy_ < 60) energy_++;
      else if (energy_ > 65) energy_--;
    }

    // 2. Curiosity: se muito tempo sem interação (> 90s), curiosity sobe muito devagar (+1 até 55)
    if (lastInteractionMs_ > 0 && (nowMs - lastInteractionMs_ > 90000)) {
      if (curiosity_ < 55) curiosity_++;
    } else {
      // Caso contrário, curiosity tende lentamente ao ponto de equilíbrio (50)
      if (curiosity_ > 50) curiosity_--;
      else if (curiosity_ < 50) curiosity_++;
    }

    // 3. Affection: decai muito lentamente para baseline acolhedor (45), nunca despenca
    if (affection_ > 45) affection_--;
    else if (affection_ < 45) affection_++;
  }
}

