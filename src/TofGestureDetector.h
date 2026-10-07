#pragma once
#include <Arduino.h>

enum class TofEvent : uint8_t {
  NONE,
  USER_PRESENT,
  USER_LEFT,
  HAND_WAVE
};

class TofGestureDetector {
 public:
  static constexpr uint16_t kPresenceEnterMm = 850;    // Confirma presenca abaixo de 850mm
  static constexpr uint16_t kPresenceExitMm = 1000;    // Confirma ausencia apenas acima de 1000mm
  static constexpr uint32_t kAbsenceConfirmMs = 15000; // 15 segundos continuos para declarar USER_LEFT
  static constexpr uint32_t kWaveCooldownMs = 2000;    // 2 segundos de cooldown apos gesto valido

  // Parametros do gesto de mao rapido (HAND_WAVE):
  // Padrao: aproximacao rapida -> pico proximo plausivel -> afastamento/reversao em janela curta
  static constexpr int16_t  kWaveMinDeltaMm = 120;     // Minimo 120mm de variacao na aproximacao
  static constexpr int16_t  kWaveRetreatDeltaMm = 80;  // Minimo 80mm de recuo/afastamento apos o pico
  static constexpr uint32_t kWaveWindowMaxMs = 700;    // Janela maxima total do gesto (aproximacao + recuo)
  static constexpr uint16_t kWavePeakMinDistMm = 60;   // Limite inferior do pico (evita colar no sensor)
  static constexpr uint16_t kWavePeakMaxDistMm = 380;  // Limite superior do pico proximo
  static constexpr int16_t  kMaxPhysJumpPerSampleMm = 350; // Salto maximo plausivel em 50ms (outlier filter)

  struct Sample {
    uint32_t timeMs;
    uint16_t distMm;
  };

  TofEvent update(uint16_t distMm, uint32_t now) {
    // Regra 1: 0 mm e sempre invalido (erro/timeout do sensor) e NUNCA entra no historico
    if (distMm == 0) {
      return TofEvent::NONE;
    }

    // Regra 2: Rejeicao de outliers aberrantes com re-sincronizacao de baseline segura
    if (sampleCount_ > 0) {
      Sample prevSample = samples_[(sampleIdx_ + kHistorySize - 1) % kHistorySize];
      int16_t stepDelta = abs(static_cast<int32_t>(distMm) - static_cast<int32_t>(prevSample.distMm));
      if (stepDelta > kMaxPhysJumpPerSampleMm) {
        // Excecao para aproximacao rapida plausivel:
        // Quando a mao entra subitamente na faixa valida de pico vindo de um fundo mais distante,
        // preservamos a amostra anterior como referencia de inicio de aproximacao (approach seed).
        if (distMm < prevSample.distMm &&
            distMm >= kWavePeakMinDistMm &&
            distMm <= kWavePeakMaxDistMm) {
          samples_[0] = prevSample;
          samples_[1] = {now, distMm};
          sampleIdx_ = 2;
          sampleCount_ = 2;
          Serial.printf("[DIAG:TOF_WAVE] APPROACH_SEED prev=%u cur=%u\n", prevSample.distMm, distMm);
          return TofEvent::NONE;
        }

        // Outlier detectado: nao permite que um unico salto gere HAND_WAVE.
        // Re-sincroniza baseline com a leitura atual descartando historico anterior
        // para nao deixar o detector preso comparando contra uma referencia antiga.
        samples_[0] = {now, distMm};
        sampleIdx_ = 1;
        sampleCount_ = 1;
        return TofEvent::NONE;
      }
    }

    // Amostra valida: armazena no buffer circular
    samples_[sampleIdx_] = {now, distMm};
    sampleIdx_ = (sampleIdx_ + 1) % kHistorySize;
    if (sampleCount_ < kHistorySize) sampleCount_++;

    TofEvent event = TofEvent::NONE;

    // Regra 3: Deteccao de Gesto Rapido de Mao (HAND_WAVE):
    // Aproximacao rapida -> Pico proximo plausivel -> Afastamento/reversao dentro da janela
    if (now - lastWaveTimeMs_ >= kWaveCooldownMs && sampleCount_ >= 4) {
      // Localiza o ponto mais proximo (pico minimo de distancia) nas amostras recentes
      int bestPeakIdx = -1;
      uint16_t peakDist = 65535;
      uint32_t peakTime = 0;

      for (size_t i = 0; i < sampleCount_; ++i) {
        const auto& s = samples_[i];
        if (now - s.timeMs <= kWaveWindowMaxMs) {
          if (s.distMm < peakDist) {
            peakDist = s.distMm;
            peakTime = s.timeMs;
            bestPeakIdx = static_cast<int>(i);
          }
        }
      }

      // O pico proximo deve estar na faixa plausivel e nao pode ser a amostra atual
      // (precisa ja ter ocorrido afastamento apos o pico)
      if (bestPeakIdx >= 0 && peakDist >= kWavePeakMinDistMm && peakDist <= kWavePeakMaxDistMm) {
        // Verifica se a amostra atual representa afastamento/reversao real apos o pico
        int16_t currentRetreat = static_cast<int16_t>(distMm) - static_cast<int16_t>(peakDist);
        uint32_t timeSincePeak = now - peakTime;

        if (currentRetreat >= kWaveRetreatDeltaMm && timeSincePeak <= 400) {
          // Agora verifica se ANTES do pico houve aproximacao rapida equivalente
          uint16_t maxBeforeDist = peakDist;
          uint32_t beforeTime = peakTime;

          for (size_t i = 0; i < sampleCount_; ++i) {
            const auto& s = samples_[i];
            if (s.timeMs < peakTime && (peakTime - s.timeMs) <= 400) {
              if (s.distMm > maxBeforeDist) {
                maxBeforeDist = s.distMm;
                beforeTime = s.timeMs;
              }
            }
          }

          int16_t approachDelta = static_cast<int16_t>(maxBeforeDist) - static_cast<int16_t>(peakDist);
          if (approachDelta >= kWaveMinDeltaMm) {
            // GESTO COMPLETO E VALIDO CONFIRMADO: aproximacao + pico + afastamento!
            lastWaveTimeMs_ = now;
            event = TofEvent::HAND_WAVE;
            Serial.printf("[DIAG:TOF_WAVE] ACEITO start=%u mm peak=%u mm end=%u mm dt_app=%lu ms dt_ret=%lu ms\n",
                          maxBeforeDist, peakDist, distMm,
                          static_cast<unsigned long>(peakTime - beforeTime),
                          static_cast<unsigned long>(now - peakTime));
            sampleCount_ = 0;
            sampleIdx_ = 0;
          }
        }
      }
    }

    // Regra 4: Presenca Estavel com Histerese (NUNCA abre o microfone)
    if (!userPresent_) {
      if (distMm <= kPresenceEnterMm) {
        if (presenceFilterCount_ < 3) {
          presenceFilterCount_++;
        } else {
          userPresent_ = true;
          absenceStartMs_ = 0;
          presenceFilterCount_ = 0;
          if (event == TofEvent::NONE) {
            event = TofEvent::USER_PRESENT;
          }
        }
      } else {
        presenceFilterCount_ = 0;
      }
    } else {
      presenceFilterCount_ = 0;
      if (distMm >= kPresenceExitMm) {
        if (absenceStartMs_ == 0) {
          absenceStartMs_ = now;
        } else if (now - absenceStartMs_ >= kAbsenceConfirmMs) {
          userPresent_ = false;
          absenceStartMs_ = 0;
          if (event == TofEvent::NONE) {
            event = TofEvent::USER_LEFT;
          }
        }
      } else {
        absenceStartMs_ = 0; // Permanece presente
      }
    }

    return event;
  }

  bool isUserPresent() const { return userPresent_; }

 private:
  static constexpr size_t kHistorySize = 16;
  Sample samples_[kHistorySize] = {};
  size_t sampleIdx_ = 0;
  size_t sampleCount_ = 0;

  bool userPresent_ = false;
  uint8_t presenceFilterCount_ = 0;
  uint32_t absenceStartMs_ = 0;
  uint32_t lastWaveTimeMs_ = 0;
};


