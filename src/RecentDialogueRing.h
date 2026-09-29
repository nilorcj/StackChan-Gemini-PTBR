#pragma once

#include <Arduino.h>

// Anel de diálogo recente volátil em RAM para continuidade conversacional de curto prazo.
// Não persiste em SD/NVS e possui pegada máxima controlada (~1.0 a 1.5 KB).
class RecentDialogueRing {
 public:
  static constexpr size_t kMaxTurns = 3;
  static constexpr uint32_t kMaxAgeMs = 300000; // Janela recente de 5 minutos
  static constexpr size_t kMaxCharBudget = 1200; // Orçamento máximo para injeção

  struct Turn {
    uint32_t timestampMs = 0;
    String userText;
    String pixText;
  };

  void onUserTranscript(const String& text, uint32_t nowMs) {
    String trimmed = text;
    trimmed.trim();
    if (!trimmed.length()) return;

    // Se o turno mais recente ainda não tem fala da Pix, atualiza a fala do usuário
    if (count_ > 0 && turns_[(head_ + count_ - 1) % kMaxTurns].pixText.length() == 0) {
      size_t lastIdx = (head_ + count_ - 1) % kMaxTurns;
      turns_[lastIdx].userText = trimmed;
      turns_[lastIdx].timestampMs = nowMs;
      return;
    }

    // Inicia um novo turno no anel circular
    size_t idx = 0;
    if (count_ < kMaxTurns) {
      idx = (head_ + count_) % kMaxTurns;
      count_++;
    } else {
      idx = head_;
      head_ = (head_ + 1) % kMaxTurns;
    }
    turns_[idx].timestampMs = nowMs;
    turns_[idx].userText = trimmed;
    turns_[idx].pixText = "";
  }

  void onPixTranscriptChunk(const char* textChunk, uint32_t nowMs) {
    if (!textChunk || !textChunk[0] || count_ == 0) return;
    String chunk(textChunk);
    chunk.trim();
    if (!chunk.length()) return;

    size_t lastIdx = (head_ + count_ - 1) % kMaxTurns;
    if (turns_[lastIdx].pixText.length() > 0) {
      char lastChar = turns_[lastIdx].pixText[turns_[lastIdx].pixText.length() - 1];
      char firstChar = chunk[0];
      bool isPunctuation = (firstChar == '.' || firstChar == ',' || firstChar == '!' ||
                            firstChar == '?' || firstChar == ':' || firstChar == ';');
      if (lastChar != ' ' && !isPunctuation) {
        turns_[lastIdx].pixText += ' ';
      }
    }
    turns_[lastIdx].pixText += chunk;
    turns_[lastIdx].timestampMs = nowMs;

    // Trava de segurança: evita crescimento excessivo de strings na RAM
    if (turns_[lastIdx].pixText.length() > 350) {
      turns_[lastIdx].pixText = turns_[lastIdx].pixText.substring(0, 347) + "...";
    }
  }

  bool hasRecentDialogue(uint32_t nowMs) const {
    for (size_t i = 0; i < count_; ++i) {
      size_t idx = (head_ + i) % kMaxTurns;
      if (turns_[idx].timestampMs > 0 && (nowMs - turns_[idx].timestampMs < kMaxAgeMs)) {
        if (turns_[idx].userText.length() > 0 || turns_[idx].pixText.length() > 0) {
          return true;
        }
      }
    }
    return false;
  }

  String formatRecentContext(uint32_t nowMs) const {
    String out;
    out.reserve(kMaxCharBudget);
    for (size_t i = 0; i < count_; ++i) {
      size_t idx = (head_ + i) % kMaxTurns;
      if (turns_[idx].timestampMs == 0 || (nowMs - turns_[idx].timestampMs >= kMaxAgeMs)) {
        continue;
      }
      uint32_t ageSec = (nowMs - turns_[idx].timestampMs) / 1000;
      if (turns_[idx].userText.length() > 0 || turns_[idx].pixText.length() > 0) {
        if (out.length() > 0) out += "\n";
        out += "(há " + String(ageSec) + "s) ";
        if (turns_[idx].userText.length() > 0) {
          out += "Usuário: \"" + turns_[idx].userText + "\"";
        }
        if (turns_[idx].pixText.length() > 0) {
          if (turns_[idx].userText.length() > 0) out += " | ";
          out += "Pix: \"" + turns_[idx].pixText + "\"";
        }
      }
    }
    return out;
  }

  size_t count() const { return count_; }
  const Turn* getTurn(size_t i) const {
    if (i >= count_) return nullptr;
    return &turns_[(head_ + i) % kMaxTurns];
  }

  void clear() {
    count_ = 0;
    head_ = 0;
    for (size_t i = 0; i < kMaxTurns; ++i) {
      turns_[i].timestampMs = 0;
      turns_[i].userText = "";
      turns_[i].pixText = "";
    }
  }

 private:
  Turn turns_[kMaxTurns];
  size_t head_ = 0;
  size_t count_ = 0;
};
