#include "PixBehaviorManager.h"
#include <esp_random.h>
#include <time.h>
#include <math.h>

PixBehaviorManager::PixBehaviorManager()
    : energy_(60), curiosity_(55), affection_(50),
      temporalEnergyBias_(0), temporalCuriosityBias_(0), lastTemporalUpdateMs_(0),
      afterglowEnergy_(0.0f), afterglowCuriosity_(0.0f), afterglowAffection_(0.0f), lastAfterglowUpdateMs_(0),
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
  if (temporalEnergyBias_ != 0 || afterglowEnergyBias() != 0 || afterglowCuriosityBias() != 0 || afterglowAffectionBias() != 0) {
    Serial.printf("[BEHAVIOR] event=%s energy=%u(eff=%u,t=%+d,a=%+d) curiosity=%u(eff=%u,t=%+d,a=%+d) affection=%u(eff=%u,a=%+d) decision=%s\n",
                  eventStr,
                  energy_, effectiveEnergy(), temporalEnergyBias_, afterglowEnergyBias(),
                  curiosity_, effectiveCuriosity(), temporalCuriosityBias_, afterglowCuriosityBias(),
                  affection_, effectiveAffection(), afterglowAffectionBias(),
                  decisionToString(decision));
  } else {
    Serial.printf("[BEHAVIOR] event=%s energy=%u curiosity=%u affection=%u decision=%s\n",
                  eventStr, energy_, curiosity_, affection_, decisionToString(decision));
  }
}

void PixBehaviorManager::logState(const char* eventTag) {
  if (temporalEnergyBias_ != 0 || afterglowEnergyBias() != 0 || afterglowCuriosityBias() != 0 || afterglowAffectionBias() != 0) {
    Serial.printf("[BEHAVIOR:STATE] event=%s energy=%u(eff=%u,t=%+d,a=%+d) curiosity=%u(eff=%u,t=%+d,a=%+d) affection=%u(eff=%u,a=%+d)\n",
                  eventTag,
                  energy_, effectiveEnergy(), temporalEnergyBias_, afterglowEnergyBias(),
                  curiosity_, effectiveCuriosity(), temporalCuriosityBias_, afterglowCuriosityBias(),
                  affection_, effectiveAffection(), afterglowAffectionBias());
  } else {
    Serial.printf("[BEHAVIOR:STATE] event=%s energy=%u curiosity=%u affection=%u\n",
                  eventTag, energy_, curiosity_, affection_);
  }
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

  // Etapa 7: Afterglow de presença - viés temporário de atenção/curiosidade (+3, max +5) por ~1.2 min
  afterglowCuriosity_ = fminf(5.0f, afterglowCuriosity_ + 3.0f);

  // Pequeno aumento de affection (+2) se já houver histórico recente de interação (últimos 2 min)
  bool hadRecentInteraction = (lastInteractionMs_ > 0 && (nowMs - lastInteractionMs_ < 120000));
  if (hadRecentInteraction) {
    if (affection_ <= 98) affection_ += 2; else affection_ = 100;
    // Etapa 7: Afterglow acolhedor suave (+2, max +4) se usuário já vinha interagindo
    afterglowAffection_ = fminf(4.0f, afterglowAffection_ + 2.0f);
  }

  // Influência de energia no limiar de iniciativa (50% base, variando de 35% a 70%)
  // Menor energia -> maior probabilidade de NO_ACTION (menos iniciativa)
  int noActionThreshold = 50 + (50 - static_cast<int>(effectiveEnergy())) * 4 / 10;
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

  // Pesos dinâmicos com base nos estados internos efetivos
  // Base: Look=35, Tilt=25, Happy=15
  int wLook = 35;
  int wTilt = 25 + ((effectiveCuriosity() > 50) ? (effectiveCuriosity() - 50) * 5 / 10 : 0);
  int wHappy = 15 + ((effectiveAffection() > 50) ? (effectiveAffection() - 50) * 6 / 10 : 0);

  // Baixa energia favorece gestos discretos (LOOK_AT_USER) sobre animações expansivas
  if (effectiveEnergy() < 40) {
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

  // Etapa 7: Afterglow pós-afago - mantém viés acolhedor (+5, max +6) por ~3 min e leve relaxamento (+1)
  afterglowAffection_ = fminf(6.0f, afterglowAffection_ + 5.0f);
  afterglowCuriosity_ = fminf(4.0f, afterglowCuriosity_ + 2.0f);
  afterglowEnergy_ = fminf(3.0f, afterglowEnergy_ + 1.0f);

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

  // Etapa 7: Afterglow pós-aceno - mantém leve atenção/curiosidade (+4, max +5) por ~1.7 min
  afterglowCuriosity_ = fminf(5.0f, afterglowCuriosity_ + 4.0f);

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

  // Etapa 7: Afterglow pós-conversa - mantém estado mais engajado/acolhedor por ~2.5 min
  afterglowAffection_ = fminf(6.0f, afterglowAffection_ + 4.0f);
  afterglowCuriosity_ = fminf(5.0f, afterglowCuriosity_ + 3.0f);
  afterglowEnergy_ = fminf(4.0f, afterglowEnergy_ + 2.0f);

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
    // Probabilidade modulada sutilmente pela energia efetiva (entre 8% e 28%, base 22%)
    int spontaneousChance = 22 + (static_cast<int>(effectiveEnergy()) - 50) * 15 / 100;
    if (spontaneousChance < 8) spontaneousChance = 8;
    if (spontaneousChance > 28) spontaneousChance = 28;

    uint32_t rollSpontaneous = esp_random() % 100;
    if (rollSpontaneous < static_cast<uint32_t>(spontaneousChance)) {
      lastSpontaneousActionMs_ = nowMs;
      spontaneousCooldownMs_ = 18000 + (esp_random() % 12001); // Cooldown rigoroso de 18-30s preservado

      // Ação espontânea consome 1 ponto de energia base
      if (energy_ > 10) energy_ -= 1;

      BehaviorDecision decision = BehaviorDecision::LOOK_AT_USER;
      // Alta afeição efetiva dá chance de um sorriso sutil espontâneo
      if (effectiveAffection() >= 65 && (esp_random() % 100) < 35) {
        decision = BehaviorDecision::HAPPY_SHORT;
      } else if (effectiveCuriosity() >= 60) {
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

void PixBehaviorManager::computeTemporalBiases() {
  time_t nowSec = time(nullptr);
  struct tm timeInfo {};
  localtime_r(&nowSec, &timeInfo);
  if (simulatedHour_ < 0 && timeInfo.tm_year < (2024 - 1900)) {
    // Horário ainda não sincronizado via NTP/RTC: viés zero (baseline neutro)
    temporalEnergyBias_ = 0;
    temporalCuriosityBias_ = 0;
    return;
  }

  int hour = (simulatedHour_ >= 0 && simulatedHour_ <= 23) ? simulatedHour_ : timeInfo.tm_hour;
  int min = (simulatedHour_ >= 0 && simulatedHour_ <= 23) ? 0 : timeInfo.tm_min;
  int minOfDay = hour * 60 + min; // 0..1439
  float bias = 0.0f;

  if (minOfDay >= 30 && minOfDay < 330) {
    // Madrugada pura (00:30 - 05:30): mais quieto e econômico (-5)
    bias = -5.0f;
  } else if (minOfDay >= 330 && minOfDay < 390) {
    // Transição suave Madrugada -> Manhã (05:30 - 06:30, janela de 60 min): -5.0 -> +5.0
    float t = static_cast<float>(minOfDay - 330) / 60.0f;
    bias = -5.0f + 10.0f * t;
  } else if (minOfDay >= 390 && minOfDay < 690) {
    // Manhã pura (06:30 - 11:30): mais ativo e atento (+5)
    bias = 5.0f;
  } else if (minOfDay >= 690 && minOfDay < 750) {
    // Transição suave Manhã -> Tarde (11:30 - 12:30, janela de 60 min): +5.0 -> 0.0
    float t = static_cast<float>(minOfDay - 690) / 60.0f;
    bias = 5.0f - 5.0f * t;
  } else if (minOfDay >= 750 && minOfDay < 1050) {
    // Tarde pura (12:30 - 17:30): neutro / baseline (0)
    bias = 0.0f;
  } else if (minOfDay >= 1050 && minOfDay < 1110) {
    // Transição suave Tarde -> Noite (17:30 - 18:30, janela de 60 min): 0.0 -> -3.0
    float t = static_cast<float>(minOfDay - 1050) / 60.0f;
    bias = 0.0f - 3.0f * t;
  } else if (minOfDay >= 1110 && minOfDay < 1410) {
    // Noite pura (18:30 - 23:30): mais calmo (-3)
    bias = -3.0f;
  } else {
    // Transição suave Noite -> Madrugada (23:30 - 00:30, janela de 60 min atravessando meia-noite): -3.0 -> -5.0
    int elapsed = (minOfDay >= 1410) ? (minOfDay - 1410) : (30 + minOfDay);
    float t = static_cast<float>(elapsed) / 60.0f;
    bias = -3.0f - 2.0f * t;
  }

  temporalEnergyBias_ = static_cast<int8_t>(roundf(bias));
  temporalCuriosityBias_ = static_cast<int8_t>(roundf(bias * 0.6f));
}

void PixBehaviorManager::update(uint32_t nowMs) {
  // Atualiza viés do contexto temporal a cada 15 segundos
  if (lastTemporalUpdateMs_ == 0 || (nowMs - lastTemporalUpdateMs_ >= 15000)) {
    lastTemporalUpdateMs_ = nowMs;
    computeTemporalBiases();
  }

  // Etapa 7: Decaimento suave do afterglow emocional (histerese de 1 a 5 min)
  if (lastAfterglowUpdateMs_ == 0) {
    lastAfterglowUpdateMs_ = nowMs;
  } else {
    uint32_t dt = nowMs - lastAfterglowUpdateMs_;
    if (dt >= 1000) {
      lastAfterglowUpdateMs_ = nowMs;
      float dtSec = static_cast<float>(dt) / 1000.0f;
      // Taxas de decaimento suave:
      // Affection: ~1 ponto a cada 35 segundos (após afago +5 dura ~175s = ~3 min)
      // Curiosity: ~1 ponto a cada 25 segundos (após aceno +4 dura ~100s = ~1.7 min)
      // Energy: ~1 ponto a cada 30 segundos (após conversa +2 dura ~60s = ~1 min)
      if (afterglowAffection_ > 0.0f) {
        afterglowAffection_ = fmaxf(0.0f, afterglowAffection_ - dtSec * (1.0f / 35.0f));
      }
      if (afterglowCuriosity_ > 0.0f) {
        afterglowCuriosity_ = fmaxf(0.0f, afterglowCuriosity_ - dtSec * (1.0f / 25.0f));
      }
      if (afterglowEnergy_ > 0.0f) {
        afterglowEnergy_ = fmaxf(0.0f, afterglowEnergy_ - dtSec * (1.0f / 30.0f));
      }
    }
  }

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

