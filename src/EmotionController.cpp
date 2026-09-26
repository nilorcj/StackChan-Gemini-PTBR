#include "EmotionController.h"
#include "UnitPaHubManager.h"
#include "LiveFeedsManager.h"

#include <M5StackChan.h>
#include <M5Unified.h>

namespace {
constexpr uint8_t kLedCount = 12;
constexpr uint32_t kFrameIntervalMs = 35;  // ~28 FPS for LEDs; smooth, but light on CPU/audio.
constexpr uint32_t kFaceIntervalMs = 80;   // ~12 FPS for idle LCD; avoid stealing time from audio.
constexpr uint32_t kSpeakingFaceIntervalMs = 240; // ~4.1 FPS while speaking; cuts SPI display bus load in half, giving max CPU to audio DMA and WiFi.
constexpr uint32_t kSleepDisplayOffMs = 15 * 60 * 1000;  // 15 minutes of sleep before blanking display

String lowerTrimmed(String s) {
  s.trim();
  s.toLowerCase();
  return s;
}

int triWave(uint16_t phase, int amplitude) {
  uint8_t p = phase & 0xFF;
  int v = p < 128 ? p : 255 - p;
  return ((v * 2 * amplitude) / 255) - amplitude;
}

// Latin-1 / UTF-8 compatible GLCD font wrapper for Font0.
// Resolves missing Portuguese accented glyphs (á à â ã é ê í ó ô õ ú ç)
// without replacing Font0 geometry or stripping accents.
class Latin1GLCDFont : public lgfx::IFont {
 public:
  font_type_t getType(void) const override { return ft_glcd; }

  void getDefaultMetric(lgfx::FontMetrics* metrics) const override {
    fonts::Font0.getDefaultMetric(metrics);
  }

  bool updateFontMetric(lgfx::FontMetrics* metrics, uint16_t uniCode) const override {
    if (uniCode <= 127) {
      return fonts::Font0.updateFontMetric(metrics, uniCode);
    }
    if (getAccentBitmap(uniCode) != nullptr) {
      metrics->width = 6;
      metrics->x_advance = 6;
      metrics->x_offset = 0;
      metrics->height = 8;
      metrics->y_advance = 8;
      metrics->y_offset = 0;
      metrics->baseline = 7;
      return true;
    }
    return false;
  }

  size_t drawChar(lgfx::LGFXBase* gfx, int32_t x, int32_t y, uint16_t c,
                  const lgfx::TextStyle* style, lgfx::FontMetrics* metrics,
                  int32_t& filled_x) const override {
    if (c <= 127) {
      return fonts::Font0.drawChar(gfx, x, y, c, style, metrics, filled_x);
    }

    const uint8_t* font_addr = getAccentBitmap(c);
    if (!font_addr) {
      return drawCharDummy(gfx, x, y, 6, 8, style, filled_x);
    }

    const int32_t fontWidth = 6;
    const int32_t fontHeight = 8;
    const int32_t datawidth = 5;

    auto cc = gfx->getColorConverter();
    bool fillbg = (style->back_rgb888 != style->fore_rgb888);
    uint32_t colortbl[2] = {cc->convert(style->back_rgb888), cc->convert(style->fore_rgb888)};

    const int32_t sy = 65536 * style->size_y;
    const int32_t sx = 65536 * style->size_x;

    gfx->startWrite();

    uint32_t x1 = 0;
    int_fast8_t i = 0;
    do {
      uint_fast8_t line = font_addr[i];
      uint_fast8_t flg = (line & 0x01);
      int_fast8_t j = 1;
      uint32_t y1 = 0;
      uint32_t x0 = x1;
      x1 = (++i * sx) >> 16;
      uint32_t w = x1 - x0;
      do {
        while (flg == ((line >> j) & 0x01) && ++j < fontHeight);
        uint32_t y0 = y1;
        y1 = (j * sy) >> 16;
        if (flg || fillbg) {
          gfx->setRawColor(colortbl[flg]);
          gfx->writeFillRect(x, y + y0, w, y1 - y0);
        }
        flg = !flg;
      } while (j < fontHeight);
      x += w;
    } while (i < datawidth);

    uint32_t x2 = (fontWidth * sx) >> 16;
    if (fillbg && datawidth < fontWidth) {
      gfx->setRawColor(colortbl[0]);
      gfx->writeFillRect(x, y, x2 - (datawidth * sx >> 16), (fontHeight * sy) >> 16);
    }

    gfx->endWrite();
    return x2;
  }

  static const uint8_t* getAccentBitmap(uint16_t c) {
    switch (c) {
      // Lowercase Portuguese accented letters
      case 0x00E1: { static const uint8_t b[] = { 0x20, 0x54, 0x54, 0x79, 0x41 }; return b; } // á
      case 0x00E0: { static const uint8_t b[] = { 0x21, 0x55, 0x54, 0x78, 0x40 }; return b; } // à
      case 0x00E2: { static const uint8_t b[] = { 0x21, 0x55, 0x55, 0x79, 0x41 }; return b; } // â
      case 0x00E3: { static const uint8_t b[] = { 0x20, 0x55, 0x57, 0x7A, 0x40 }; return b; } // ã
      case 0x00E9: { static const uint8_t b[] = { 0x38, 0x54, 0x54, 0x55, 0x59 }; return b; } // é
      case 0x00EA: { static const uint8_t b[] = { 0x39, 0x55, 0x55, 0x55, 0x59 }; return b; } // ê
      case 0x00ED: { static const uint8_t b[] = { 0x00, 0x00, 0x44, 0x7D, 0x41 }; return b; } // í
      case 0x00F3: { static const uint8_t b[] = { 0x30, 0x48, 0x48, 0x4A, 0x32 }; return b; } // ó
      case 0x00F4: { static const uint8_t b[] = { 0x32, 0x49, 0x49, 0x49, 0x32 }; return b; } // ô
      case 0x00F5: { static const uint8_t b[] = { 0x38, 0x45, 0x47, 0x46, 0x38 }; return b; } // õ
      case 0x00FA: { static const uint8_t b[] = { 0x38, 0x40, 0x40, 0x22, 0x7A }; return b; } // ú
      case 0x00E7: { static const uint8_t b[] = { 0x0C, 0x1E, 0x52, 0x72, 0x12 }; return b; } // ç

      // Uppercase Portuguese accented letters
      case 0x00C1: { static const uint8_t b[] = { 0x7C, 0x12, 0x11, 0x12, 0x7C }; return b; } // Á
      case 0x00C0: { static const uint8_t b[] = { 0x7C, 0x11, 0x12, 0x12, 0x7C }; return b; } // À
      case 0x00C2: { static const uint8_t b[] = { 0x7C, 0x13, 0x11, 0x13, 0x7C }; return b; } // Â
      case 0x00C3: { static const uint8_t b[] = { 0x7C, 0x13, 0x12, 0x13, 0x7C }; return b; } // Ã
      case 0x00C9: { static const uint8_t b[] = { 0x7C, 0x54, 0x55, 0x45, 0x00 }; return b; } // É
      case 0x00CA: { static const uint8_t b[] = { 0x7C, 0x55, 0x55, 0x45, 0x00 }; return b; } // Ê
      case 0x00CD: { static const uint8_t b[] = { 0x00, 0x41, 0x7D, 0x41, 0x00 }; return b; } // Í
      case 0x00D3: { static const uint8_t b[] = { 0x38, 0x44, 0x45, 0x45, 0x38 }; return b; } // Ó
      case 0x00D4: { static const uint8_t b[] = { 0x38, 0x45, 0x45, 0x45, 0x38 }; return b; } // Ô
      case 0x00D5: { static const uint8_t b[] = { 0x38, 0x45, 0x46, 0x45, 0x38 }; return b; } // Õ
      case 0x00DA: { static const uint8_t b[] = { 0x3C, 0x40, 0x40, 0x41, 0x3D }; return b; } // Ú
      case 0x00C7: { static const uint8_t b[] = { 0x1E, 0xA1, 0xA1, 0x61, 0x12 }; return b; } // Ç

      default: return nullptr;
    }
  }
};

static const Latin1GLCDFont s_latin1Font;
}  // namespace

void EmotionController::begin() {
  mouthSprite_.setColorDepth(16);
  mouthSprite_.setPsram(false);
  void* pMouth = mouthSprite_.createSprite(kMouthSpriteW, kMouthSpriteH);

  leftEyeSprite_.setColorDepth(16);
  leftEyeSprite_.setPsram(false);
  void* pLeft = leftEyeSprite_.createSprite(kLeftEyeSpriteW, kLeftEyeSpriteH);

  rightEyeSprite_.setColorDepth(16);
  rightEyeSprite_.setPsram(false);
  void* pRight = rightEyeSprite_.createSprite(kRightEyeSpriteW, kRightEyeSpriteH);

  if (pMouth != nullptr && pLeft != nullptr && pRight != nullptr) {
    partialRenderingEnabled_ = true;
    Serial.printf("Emotion: Partial sprites created in internal SRAM (mouth=%p 100x54, left=%p 96x80, right=%p 96x80)\n",
                  pMouth, pLeft, pRight);
  } else {
    partialRenderingEnabled_ = false;
    mouthSprite_.deleteSprite();
    leftEyeSprite_.deleteSprite();
    rightEyeSprite_.deleteSprite();
    Serial.println("Emotion: Partial sprites SRAM allocation failed, falling back to full-screen canvas");
  }

  randomSeed(esp_random());
  resetRestHumanization();
  setEmotion("neutral");
}

void EmotionController::resetRestHumanization() {
  blinkState_ = BlinkState::OPEN;
  nextBlinkDeadlineMs_ = 0;
  blinkStageDeadlineMs_ = 0;
  isBlinking_ = false;
  targetPupilDx_ = 0;
  targetPupilDy_ = 0;
  currentPupilDx_ = 0.0f;
  currentPupilDy_ = 0.0f;
  nextPupilDriftMs_ = 0;
}

void EmotionController::updateBlinkState(uint32_t now) {
  bool inRest = (mode_ == Mode::Neutral && turnStateIdle_ && !conversationArmed_ &&
                 !isSpeaking_ && !micActive_ && !isLifted_ && !isShaken_ && !isDnd_ && !sleepDisplayOff_);
  if (!inRest) {
    if (blinkState_ != BlinkState::OPEN || isBlinking_) {
      resetRestHumanization();
    }
    return;
  }

  bool prevBlinking = isBlinking_;

  switch (blinkState_) {
    case BlinkState::OPEN: {
      isBlinking_ = false;
      if (nextBlinkDeadlineMs_ == 0) {
        nextBlinkDeadlineMs_ = now + static_cast<uint32_t>(random(2500, 6001));
      } else if (static_cast<int32_t>(now - nextBlinkDeadlineMs_) >= 0) {
        blinkState_ = BlinkState::FIRST_CLOSED;
        blinkStageDeadlineMs_ = now + 130;
        isBlinking_ = true;
      }
      break;
    }
    case BlinkState::FIRST_CLOSED: {
      isBlinking_ = true;
      if (static_cast<int32_t>(now - blinkStageDeadlineMs_) >= 0) {
        // ~15% chance of double blink
        if (random(100) < 15) {
          blinkState_ = BlinkState::DOUBLE_WAIT;
          uint32_t pause = static_cast<uint32_t>(random(120, 221));
          blinkStageDeadlineMs_ = now + pause;
          isBlinking_ = false;
        } else {
          blinkState_ = BlinkState::OPEN;
          nextBlinkDeadlineMs_ = now + static_cast<uint32_t>(random(2500, 6001));
          isBlinking_ = false;
        }
      }
      break;
    }
    case BlinkState::DOUBLE_WAIT: {
      isBlinking_ = false;
      if (static_cast<int32_t>(now - blinkStageDeadlineMs_) >= 0) {
        blinkState_ = BlinkState::SECOND_CLOSED;
        blinkStageDeadlineMs_ = now + 110;
        isBlinking_ = true;
      }
      break;
    }
    case BlinkState::SECOND_CLOSED: {
      isBlinking_ = true;
      if (static_cast<int32_t>(now - blinkStageDeadlineMs_) >= 0) {
        blinkState_ = BlinkState::OPEN;
        nextBlinkDeadlineMs_ = now + static_cast<uint32_t>(random(2500, 6001));
        isBlinking_ = false;
      }
      break;
    }
  }

  if (isBlinking_ != prevBlinking) {
    needsFaceRedraw_ = true;
  }
}

void EmotionController::updatePupilDrift(uint32_t now) {
  bool inRest = (mode_ == Mode::Neutral && turnStateIdle_ && !conversationArmed_ &&
                 !isSpeaking_ && !micActive_ && !isLifted_ && !isShaken_ && !isDnd_ && !sleepDisplayOff_);
  if (!inRest) {
    targetPupilDx_ = 0;
    targetPupilDy_ = 0;
    currentPupilDx_ = 0.0f;
    currentPupilDy_ = 0.0f;
    nextPupilDriftMs_ = 0;
    return;
  }

  if (nextPupilDriftMs_ == 0) {
    nextPupilDriftMs_ = now + static_cast<uint32_t>(random(1500, 4001));
  } else if (static_cast<int32_t>(now - nextPupilDriftMs_) >= 0) {
    // 35% chance to center (0,0), 65% chance to drift subtler (-4..+4, -2..+2)
    if (random(100) < 35) {
      targetPupilDx_ = 0;
      targetPupilDy_ = 0;
    } else {
      targetPupilDx_ = static_cast<int>(random(-4, 5));
      targetPupilDy_ = static_cast<int>(random(-2, 3));
    }
    nextPupilDriftMs_ = now + static_cast<uint32_t>(random(1500, 4001));
  }

  float diffX = static_cast<float>(targetPupilDx_) - currentPupilDx_;
  float diffY = static_cast<float>(targetPupilDy_) - currentPupilDy_;

  if (fabs(diffX) < 0.08f) {
    currentPupilDx_ = static_cast<float>(targetPupilDx_);
  } else {
    currentPupilDx_ += diffX * 0.20f;
  }

  if (fabs(diffY) < 0.08f) {
    currentPupilDy_ = static_cast<float>(targetPupilDy_);
  } else {
    currentPupilDy_ += diffY * 0.20f;
  }
}

bool EmotionController::setEmotion(const String& emotion) {
  resetRestHumanization();
  String normalized;
  Mode next = parseMode(emotion, normalized);
  Mode previous = mode_;
  mode_ = next;
  current_ = normalized;
  frame_ = 0;
  lastFrameMs_ = 0;
  lastFaceMs_ = 0;
  sleepStartedMs_ = (mode_ == Mode::Sleep) ? millis() : 0;
  if (previous == Mode::Sleep && mode_ != Mode::Sleep) {
    restoreSleepDisplay();
  }

  // Physical haptic empathy: link emotional shifts to the N20 Unit Vibrator
  if (unitPaHub_) {
    if (mode_ == Mode::Happy || mode_ == Mode::Meigo) {
      unitPaHub_->purr(); // Gentle haptic purring on happiness/petting
    } else if (mode_ == Mode::Angry || mode_ == Mode::Error || mode_ == Mode::Scared) {
      unitPaHub_->shiverAngry(); // Indignant vibration when shaken/angry/scared
    } else if (mode_ == Mode::Found || mode_ == Mode::Surprised) {
      unitPaHub_->buzzAlert(); // Double confirmation pulse
    }
  }

  // Mark face dirty for asynchronous redraw in loop() — no pushSprite inside callbacks!
  needsFaceRedraw_ = true;
  forceFullRedraw_ = true;
  Serial.printf("Emotion: %s\n", current_.c_str());
  return true;
}

void EmotionController::setIsSpeaking(bool speaking) {
  if (isSpeaking_ == speaking) return;
  isSpeaking_ = speaking;
  if (isSpeaking_) resetRestHumanization();
  Serial.printf("Face: mouth %s (reason: speaker playback %s)\n",
                speaking ? "TALKING" : "RESTING",
                speaking ? "active" : "idle");
  needsFaceRedraw_ = true;
  forceFullRedraw_ = true;
  lastFaceMs_ = 0; // Trigger immediate face refresh on speech transition
}

void EmotionController::loop() {
  uint32_t now = millis();
  if (mode_ == Mode::Sleep && sleepStartedMs_ == 0) sleepStartedMs_ = now;
  if (mode_ != Mode::Sleep && sleepDisplayOff_) restoreSleepDisplay();

  bool externalPower = externalPowerPresent();
  if (mode_ == Mode::Sleep && !sleepDisplayOff_ && sleepVisualOffDue()) {
    enterSleepDisplayOff(externalPower);
  }

  // Update rest humanization (organic blink & pupil drift)
  updateBlinkState(now);
  updatePupilDrift(now);

  uint32_t faceInterval = isSpeaking_ ? kSpeakingFaceIntervalMs : kFaceIntervalMs;
  if (!sleepDisplayOff_ && (needsFaceRedraw_ || lastFaceMs_ == 0 || now - lastFaceMs_ >= faceInterval)) {
    needsFaceRedraw_ = false;
    lastFaceMs_ = now;
    renderFace();
  }
  if (lastFrameMs_ != 0 && now - lastFrameMs_ < kFrameIntervalMs) return;
  lastFrameMs_ = now;
  ++frame_;

  switch (mode_) {
    case Mode::Neutral:   renderNeutral(); break;
    case Mode::Listening: renderListening(); break;
    case Mode::Speaking:  renderSpeaking(); break;
    case Mode::Thinking:  renderThinking(); break;
    case Mode::Looking:   renderLooking(); break;
    case Mode::Happy:     renderHappy(); break;
    case Mode::Angry:     renderAngry(); break;
    case Mode::Found:     renderFound(); break;
    case Mode::Error:     renderError(); break;
    case Mode::Meigo:     renderMeigo(); break;
    case Mode::Scared:    renderScared(); break;
    case Mode::Surprised: renderSurprised(); break;
    case Mode::Sleep:
      if (sleepDisplayOff_ && !externalPower) setAll(0, 0, 0);
      else renderSleep();
      break;
  }
  refresh();
}

EmotionController::Mode EmotionController::parseMode(const String& emotion, String& normalized) {
  String e = lowerTrimmed(emotion);
  if (e == "listen" || e == "listening") { normalized = "listening"; return Mode::Listening; }
  if (e == "speak" || e == "speaking" || e == "talking") { normalized = "speaking"; return Mode::Speaking; }
  if (e == "think" || e == "thinking") { normalized = "thinking"; return Mode::Thinking; }
  if (e == "look" || e == "looking" || e == "seeing" || e == "camera") { normalized = "looking"; return Mode::Looking; }
  if (e == "happy" || e == "joy" || e == "smile") { normalized = "happy"; return Mode::Happy; }
  if (e == "meigo" || e == "carinho" || e == "afeto" || e == "affection") { normalized = "meigo"; return Mode::Meigo; }
  if (e == "scared" || e == "assustada" || e == "medo") { normalized = "scared"; return Mode::Scared; }
  if (e == "surprised" || e == "surpresa" || e == "espanto") { normalized = "surprised"; return Mode::Surprised; }
  if (e == "angry" || e == "mad" || e == "irritated") { normalized = "angry"; return Mode::Angry; }
  if (e == "found" || e == "success") { normalized = "found"; return Mode::Found; }
  if (e == "error" || e == "confused") { normalized = "error"; return Mode::Error; }
  if (e == "sleep" || e == "sleeping") { normalized = "sleep"; return Mode::Sleep; }
  normalized = "neutral";
  return Mode::Neutral;
}

void EmotionController::hsvToRgb(uint8_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
  uint8_t region = h / 43;
  uint8_t remainder = (h - (region * 43)) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
    default:
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
  }
}

uint8_t EmotionController::wave8(uint16_t x, uint8_t minValue, uint8_t maxValue) {
  uint8_t phase = x & 0xFF;
  uint8_t tri = phase < 128 ? phase * 2 : (255 - phase) * 2;
  return minValue + ((uint16_t)(maxValue - minValue) * tri) / 255;
}

uint8_t EmotionController::scale8(uint8_t value, uint8_t scale) {
  return ((uint16_t)value * scale) / 255;
}

bool EmotionController::externalPowerPresent() {
  int16_t vbus = M5.Power.getVBUSVoltage();
  if (vbus > 4200) return true;
  return M5.Power.isCharging() == m5::Power_Class::is_charging;
}

bool EmotionController::sleepVisualOffDue() const {
  return mode_ == Mode::Sleep && sleepStartedMs_ != 0 && millis() - sleepStartedMs_ >= kSleepDisplayOffMs;
}

void EmotionController::enterSleepDisplayOff(bool externalPower) {
  if (sleepDisplayOff_) return;
  auto& display = M5StackChan.Display();
  savedDisplayBrightness_ = display.getBrightness();
  display.fillScreen(TFT_BLACK);
  display.sleep();
  sleepDisplayOff_ = true;
  if (!externalPower) setAll(0, 0, 0);
  refresh();
  Serial.printf("Emotion: sleep display off, external_power=%s\n", externalPower ? "yes" : "no");
}

void EmotionController::restoreSleepDisplay() {
  if (!sleepDisplayOff_) return;
  auto& display = M5StackChan.Display();
  display.wakeup();
  display.setBrightness(savedDisplayBrightness_ == 0 ? 127 : savedDisplayBrightness_);
  sleepDisplayOff_ = false;
  lastFaceMs_ = 0;
  needsFaceRedraw_ = true;
  forceFullRedraw_ = true;
  Serial.println("Emotion: sleep display restored");
}

void EmotionController::setLed(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (index >= kLedCount) return;
  M5StackChan.setRgbColor(index, r, g, b);
}

void EmotionController::setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t i = 0; i < kLedCount; ++i) setLed(i, r, g, b);
}

void EmotionController::refresh() {
  M5StackChan.refreshRgb();
}

void EmotionController::drawLabel() {
  renderFace();
}

EmotionController::FaceState EmotionController::computeFaceState() const {
  FaceState s;
  s.isAngry = (mode_ == Mode::Angry || mode_ == Mode::Error);
  s.isHappy = (mode_ == Mode::Happy || mode_ == Mode::Found);
  s.isMeigo = (mode_ == Mode::Meigo || isProlongedPetting_ || current_ == "meigo" || current_ == "affection");
  s.isScared = (mode_ == Mode::Scared);
  s.isSurprised = (mode_ == Mode::Surprised);
  s.isSpeaking = isSpeaking_; // Governado ESTRITAMENTE pela reprodução real de áudio
  s.isThinking = (mode_ == Mode::Thinking);
  s.isLooking = (mode_ == Mode::Looking);
  s.isSleep = (mode_ == Mode::Sleep);

  s.irisColor = 0x3DFE;   // Sky blue
  s.glowAccent = 0x037A; // Deep blue
  s.blushColor = 0xF9F2; // Radiant pink
  s.pupilDx = 0;
  s.pupilDy = 0;
  s.closed = false;

  if (s.isAngry) {
    s.irisColor = 0xFA48;   // Vibrant Crimson Red (#EF4444)
    s.glowAccent = 0xB000;  // Dark Crimson
    s.blushColor = 0xF948;  // Flushed Red
    s.pupilDx = triWave(frame_ * 6, 4);
    s.pupilDy = triWave(frame_ * 4, 3);
  } else if (s.isScared || s.isSurprised) {
    s.irisColor = 0x05BF;   // Electric Alert Cyan
    s.glowAccent = 0xFA48;  // Crimson accent
    s.blushColor = 0xFA48;  // Startled flushed cheeks
    s.pupilDx = triWave(frame_ * 8, 4); // Fast startled pupil tremor
    s.pupilDy = triWave(frame_ * 6, 3);
  } else if (s.isMeigo) {
    s.irisColor = 0xFE44;   // Warm Amber Gold
    s.glowAccent = 0xD460;
    s.blushColor = 0xF9F2;  // Radiant Rosy Pink Cheeks
    s.pupilDx = 0;
    s.pupilDy = 0;
  } else if (s.isHappy) {
    s.irisColor = 0xFE44;   // Warm Amber Gold (#F59E0B)
    s.glowAccent = 0xD460;
    s.blushColor = 0xFD54;  // Warm Radiant Rose
    s.pupilDx = triWave(frame_ * 4, 6);
  } else if (s.isSpeaking) {
    s.irisColor = 0x1652;   // Emerald Green (#10B981)
    s.glowAccent = 0x0B86;
    s.blushColor = 0xF9F2;
    s.pupilDx = triWave(frame_ * 7, 6);
    s.pupilDy = triWave(frame_ * 4, 3);
  } else if (s.isThinking) {
    s.irisColor = 0xA2B8;   // Dreamy Violet (#A855F7)
    s.glowAccent = 0x7170;
    s.pupilDx = triWave(frame_ * 4, 10);
    s.pupilDy = triWave(frame_ * 3, 5);
  } else if (s.isLooking) {
    s.irisColor = 0x05BF;   // Electric Cyan
    s.glowAccent = 0x037A;
    s.pupilDx = triWave(frame_ * 5, 12);
  } else if (s.isSleep) {
    s.closed = true;
    s.irisColor = 0x1968;
    s.glowAccent = 0x08C4;
  } else {
    // Mode::Neutral
    bool inRest = (mode_ == Mode::Neutral && turnStateIdle_ && !conversationArmed_ &&
                   !isSpeaking_ && !micActive_ && !isLifted_ && !isShaken_ && !isDnd_ && !sleepDisplayOff_);
    if (inRest) {
      s.closed = isBlinking_;
      // Integer rounding ensures eyesDirty only triggers when drawn integer pixel actually changes
      s.pupilDx = static_cast<int>(roundf(currentPupilDx_));
      s.pupilDy = static_cast<int>(roundf(currentPupilDy_));
    } else {
      s.closed = false;
      s.pupilDx = 0;
      s.pupilDy = 0;
    }
  }

  s.talkShape = (frame_ * 2) % 8;
  s.talkYOffset = triWave(frame_ * 22, 3);

  return s;
}

void EmotionController::drawEye(LovyanGFX& d, int ex, int eyCenter, bool leftSide, const FaceState& s) {
  const int eyeR = 38;
  if (s.isMeigo) {
    // Warm squinting happy arcs (^^)
    d.fillCircle(ex, eyCenter, eyeR + 2, 0xF9F2); // radiant soft pink glow
    d.fillCircle(ex, eyCenter, eyeR, TFT_BLACK);
    d.fillRoundRect(ex - 26, eyCenter + 4, 52, 6, 3, 0xFE44); // Gold arc
    d.fillRoundRect(ex - 24, eyCenter + 5, 48, 4, 2, TFT_WHITE);
    // Cute upward lashes
    d.drawLine(ex - 20, eyCenter + 2, ex - 26, eyCenter - 4, 0xFE44);
    d.drawLine(ex + 20, eyCenter + 2, ex + 26, eyCenter - 4, 0xFE44);
    return;
  }
  if (s.isSleep) {
    // Gentle closed eyelid arch with downward lashes and sparkles (matching screenshot exactly)
    d.fillCircle(ex, eyCenter, eyeR + 2, 0x08C4); // Navy glow ring
    d.fillCircle(ex, eyCenter, eyeR, 0x0002);     // Dark blue pod
    d.fillRoundRect(ex - 28, eyCenter + 4, 56, 5, 2, 0x3DFE); // Cyan eyelid line
    d.fillRoundRect(ex - 26, eyCenter + 5, 52, 3, 1, TFT_WHITE);
    // Downward eyelashes (\ | /)
    d.drawLine(ex - 16, eyCenter + 9, ex - 24, eyCenter + 19, 0x3DFE);
    d.drawLine(ex, eyCenter + 9, ex, eyCenter + 21, 0x3DFE);
    d.drawLine(ex + 16, eyCenter + 9, ex + 24, eyCenter + 19, 0x3DFE);
    // Sparkle dots above
    d.fillCircle(ex - 10, eyCenter - 14, 3, TFT_WHITE);
    d.fillCircle(ex + 14, eyCenter - 10, 2, TFT_WHITE);
    return;
  }
  if (s.closed) {
    // Gentle closed eyelid line with feminine lashes (blink)
    d.fillRoundRect(ex - 28, eyCenter - 2, 56, 6, 3, s.glowAccent);
    d.fillRoundRect(ex - 26, eyCenter - 1, 52, 4, 2, TFT_WHITE);
    if (leftSide) {
      d.drawLine(ex - 28, eyCenter, ex - 34, eyCenter - 6, s.glowAccent);
      d.drawLine(ex - 24, eyCenter + 1, ex - 28, eyCenter + 7, s.glowAccent);
    } else {
      d.drawLine(ex + 28, eyCenter, ex + 34, eyCenter - 6, s.glowAccent);
      d.drawLine(ex + 24, eyCenter + 1, ex + 28, eyCenter + 7, s.glowAccent);
    }
    return;
  }

  // Outer glow rim
  d.fillCircle(ex, eyCenter, eyeR + 3, s.glowAccent);
  // Sclera (Pure White)
  d.fillCircle(ex, eyCenter, eyeR, TFT_WHITE);

  // Large Iris
  int px = ex + s.pupilDx;
  int py = eyCenter + s.pupilDy;
  d.fillCircle(px, py, 22, s.irisColor);
  d.fillCircle(px, py, 20, s.irisColor);

  // Dark Pupil
  d.fillCircle(px, py, 11, TFT_BLACK);

  // Specular Highlight Sparkles
  d.fillCircle(px + 6, py - 6, 4, TFT_WHITE);
  d.fillCircle(px - 4, py + 5, 2, TFT_WHITE);

  // --- EYELIDS & SHAPES ---
  if (s.isAngry) {
    if (leftSide) {
      d.fillTriangle(ex - eyeR - 5, eyCenter - eyeR - 5,
                     ex + eyeR + 5, eyCenter - eyeR - 5,
                     ex - eyeR - 5, eyCenter - 4, TFT_BLACK);
      d.fillTriangle(ex + eyeR + 5, eyCenter - eyeR - 5,
                     ex - eyeR - 5, eyCenter - 4,
                     ex + eyeR + 5, eyCenter + 10, TFT_BLACK);
      d.drawLine(ex - eyeR - 4, eyCenter - 4, ex + eyeR + 4, eyCenter + 10, s.glowAccent);
      d.drawLine(ex - eyeR - 4, eyCenter - 5, ex + eyeR + 4, eyCenter + 9, s.glowAccent);
    } else {
      d.fillTriangle(ex - eyeR - 5, eyCenter - eyeR - 5,
                     ex + eyeR + 5, eyCenter - eyeR - 5,
                     ex + eyeR + 5, eyCenter - 4, TFT_BLACK);
      d.fillTriangle(ex - eyeR - 5, eyCenter - eyeR - 5,
                     ex + eyeR + 5, eyCenter - 4,
                     ex - eyeR - 5, eyCenter + 10, TFT_BLACK);
      d.drawLine(ex + eyeR + 4, eyCenter - 4, ex - eyeR - 4, eyCenter + 10, s.glowAccent);
      d.drawLine(ex + eyeR + 4, eyCenter - 5, ex - eyeR - 4, eyCenter + 9, s.glowAccent);
    }
  } else if (s.isHappy) {
    d.fillCircle(ex, eyCenter + 32, 28, TFT_BLACK);
    d.drawCircle(ex, eyCenter + 32, 28, s.glowAccent);
  } else if (s.isScared || s.isSurprised) {
    // Wide open startled eyes: no top eyelid clipping!
    d.drawCircle(ex, eyCenter, eyeR + 2, 0xFA48);
  } else {
    d.fillRect(ex - eyeR - 2, eyCenter - eyeR - 2, (eyeR * 2) + 4, 18, TFT_BLACK);
    d.drawLine(ex - eyeR, eyCenter - eyeR + 16, ex + eyeR, eyCenter - eyeR + 16, s.glowAccent);
  }

  // Top Eyelash Accent
  if (leftSide) {
    d.drawLine(ex - eyeR, eyCenter - eyeR + 10, ex - eyeR - 6, eyCenter - eyeR + 3, s.glowAccent);
    d.drawLine(ex - eyeR + 2, eyCenter - eyeR + 8, ex - eyeR - 4, eyCenter - eyeR + 1, s.glowAccent);
  } else {
    d.drawLine(ex + eyeR, eyCenter - eyeR + 10, ex + eyeR + 6, eyCenter - eyeR + 3, s.glowAccent);
    d.drawLine(ex + eyeR - 2, eyCenter - eyeR + 8, ex + eyeR + 4, eyCenter - eyeR + 1, s.glowAccent);
  }
}

void EmotionController::drawMouth(LovyanGFX& d, int cx, int mouthY, const FaceState& s) {
  if (s.isAngry) {
    const int mw = 36;
    for (int t = -mw; t <= mw; ++t) {
      float norm = static_cast<float>(t) / static_cast<float>(mw);
      int dy = static_cast<int>(12.0f * (1.0f - norm * norm));
      d.drawPixel(cx + t, mouthY + 12 - dy, s.irisColor);
      d.drawPixel(cx + t, mouthY + 13 - dy, s.irisColor);
      d.drawPixel(cx + t, mouthY + 14 - dy, s.glowAccent);
    }
  } else if (s.isSpeaking) {
    const int openH[] = {8, 26, 14, 34, 10, 24, 18, 30};
    const int openW[] = {44, 68, 80, 56, 40, 78, 62, 70};
    int ow = openW[s.talkShape];
    int oh = openH[s.talkShape];
    int y = mouthY + s.talkYOffset;
    d.fillRoundRect(cx - ow / 2 - 4, y - oh / 2 - 3, ow + 8, oh + 6, 10, s.glowAccent);
    d.fillRoundRect(cx - ow / 2, y - oh / 2, ow, oh, 8, TFT_BLACK);
    if (oh > 20) {
      d.fillRect(cx - ow / 2 + 8, y - oh / 2 + 2, ow - 16, 4, TFT_WHITE);
    }
    if ((s.talkShape % 4) == 1) {
      d.fillCircle(cx, y + oh / 2 - 4, min(12, oh / 3), 0xF948);
    }
  } else if (s.isMeigo || s.isHappy) {
    const int mw = 44;
    for (int t = -mw; t <= mw; ++t) {
      float norm = static_cast<float>(t) / static_cast<float>(mw);
      int dy = static_cast<int>(12.0f * (1.0f - norm * norm));
      d.drawPixel(cx + t, mouthY + dy, s.irisColor);
      d.drawPixel(cx + t, mouthY + dy + 1, s.irisColor);
      d.drawPixel(cx + t, mouthY + dy + 2, s.glowAccent);
    }
  } else if (s.isScared || s.isSurprised) {
    // Open "O" mouth in surprise / fright
    d.fillCircle(cx, mouthY + 4, 13, s.glowAccent);
    d.fillCircle(cx, mouthY + 4, 10, TFT_BLACK);
    d.fillCircle(cx, mouthY + 6, 5, 0xF948);
  } else if (s.isSleep) {
    // Sleep text only drawn on full-screen canvas
  } else {
    // Neutral calm lips
    d.fillRoundRect(cx - 24, mouthY, 48, 5, 2, s.glowAccent);
    d.fillRoundRect(cx - 22, mouthY + 1, 44, 3, 1, TFT_WHITE);
  }
}

void EmotionController::renderFullFace(LovyanGFX& display, const FaceState& s) {
  static M5Canvas canvas(&display);
  static bool canvasReady = false;
  if (!canvasReady) {
    canvas.setColorDepth(16);
    canvasReady = canvas.createSprite(display.width(), display.height()) != nullptr;
    if (!canvasReady) {
      canvas.setColorDepth(8);
      canvasReady = canvas.createSprite(display.width(), display.height()) != nullptr;
    }
    if (!canvasReady) {
      Serial.println("Emotion: face canvas allocation failed");
      return;
    }
  }

  auto& d = canvas;
  const int w = display.width();
  const int h = display.height();
  const int cx = w / 2;
  const int cy = h / 2;
  d.fillSprite(TFT_BLACK);

  // Eye centers
  const int lx = cx - 58;
  const int rx = cx + 58;
  const int ey = cy - 20;

  // Top warning banner: Shaking vs Lifted from desk
  if (isShaken_) {
    d.fillRoundRect(cx - 105, 4, 210, 20, 10, 0xFA48); // Vibrant Crimson
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString("! SACUDIDA DETECTADA !", cx, 14);
  } else if (isLifted_) {
    d.fillRoundRect(cx - 95, 4, 190, 20, 10, 0xFD40); // Amber/Orange
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString("! LEVANTADA DA MESA !", cx, 14);
  }

  // Sleep mode: alternate between the Sleeping Face and Dream Card Popup every 3.5 seconds
  if (s.isSleep) {
    bool showDreamCard = ((millis() / 3500) % 2) == 1;
    if (showDreamCard) {
      int cardX = 16, cardY = 32, cardW = 288, cardH = 150;
      d.fillRoundRect(cardX, cardY, cardW, cardH, 16, 0x10A6); // Dark violet container
      d.drawRoundRect(cardX, cardY, cardW, cardH, 16, 0x51F5); // Glowing indigo border
      d.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 15, 0x31B0);

      uint8_t dreamIdx = (millis() / 7000) % 15;
      LiveFeedsManager::DreamCardData dream = liveFeeds_ ? liveFeeds_->getDreamCard(dreamIdx) : LiveFeedsManager::DreamCardData{"Natureza", "Plantar horta", "Cuidar das mudas de manjericao"};

      // Header: Thought bubble + SONHANDO ACORDADA / MODO NAO PERTURBE
      d.fillCircle(cardX + 22, cardY + 22, 6, TFT_WHITE);
      d.fillCircle(cardX + 14, cardY + 26, 3, TFT_WHITE);
      d.setTextDatum(textdatum_t::middle_left);
      d.setTextSize(1);
      d.setTextColor(isDnd_ ? 0xFDA0 : 0xA2B8); // Gold/Amber if DND, dreamy lavender otherwise
      d.drawString(isDnd_ ? "MODO NAO PERTURBE" : "SONHANDO ACORDADA", cardX + 34, cardY + 22);

      // Tag Pill on top right
      d.fillRoundRect(cardX + cardW - 90, cardY + 12, 76, 20, 10, isDnd_ ? 0x9800 : 0x7170); // Amber/Purple tag pill
      d.setTextDatum(textdatum_t::middle_center);
      d.setTextColor(TFT_WHITE);
      d.drawString(isDnd_ ? "DND" : dream.tag, cardX + cardW - 52, cardY + 22);

      // Title (large font)
      d.setTextDatum(textdatum_t::top_left);
      d.setTextSize(2);
      d.setTextColor(TFT_WHITE);
      d.drawString(dream.title, cardX + 20, cardY + 54);

      // Detail / Subtitle
      d.setTextSize(1);
      d.setTextColor(0x9DF8); // Light cyan/lavender
      d.drawString(dream.detail, cardX + 20, cardY + 92);

      // Bottom subtitle
      d.setTextDatum(textdatum_t::bottom_center);
      d.setTextSize(1);
      d.setTextColor(0x5AEB); // Soft cyan
      d.drawString(isDnd_ ? "TOQUE P/ SAIR DO DND" : "TOQUE P/ ACORDAR", cx, h - 8);

      if (isShaken_) {
        d.fillRoundRect(cx - 105, 4, 210, 20, 10, 0xFA48);
        d.setTextDatum(textdatum_t::middle_center);
        d.setTextSize(1);
        d.setTextColor(TFT_WHITE);
        d.drawString("! SACUDIDA DETECTADA !", cx, 14);
      } else if (isLifted_) {
        d.fillRoundRect(cx - 95, 4, 190, 20, 10, 0xFD40);
        d.setTextDatum(textdatum_t::middle_center);
        d.setTextSize(1);
        d.setTextColor(TFT_WHITE);
        d.drawString("! LEVANTADA DA MESA !", cx, 14);
      }

      drawQuickStatusDrawer(d);
      canvas.pushSprite(&display, 0, 0);
      return;
    }
  }

  // Draw Radiant Blush Cheeks under eyes
  if (s.isSleep) {
    // Soft horizontal purple blush pills under eyes matching screenshot
    d.fillRoundRect(lx - 22, cy + 28, 44, 8, 4, 0x5178);
    d.fillRoundRect(rx - 22, cy + 28, 44, 8, 4, 0x5178);
  } else {
    d.fillCircle(cx - 96, cy + 30, 13, s.blushColor);
    d.fillCircle(cx + 96, cy + 30, 13, s.blushColor);
    d.fillCircle(cx - 96, cy + 30, 6, 0xFFFF);
    d.fillCircle(cx + 96, cy + 30, 6, 0xFFFF);
  }

  drawEye(canvas, lx, ey, true, s);
  drawEye(canvas, rx, ey, false, s);

  int mouthY = cy + 50;
  if (s.isSleep) {
    // Floating z Z z below eyes
    int zY = cy + 42 + triWave(frame_ * 3, 4);
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextSize(2);
    d.setTextColor(0x3DFE);
    d.drawString(isDnd_ ? "z Z z (DND)" : "z Z z", cx, zY);
    d.setTextDatum(textdatum_t::bottom_center);
    d.setTextSize(1);
    d.setTextColor(isDnd_ ? 0xFDA0 : 0x5AEB);
    d.drawString(isDnd_ ? "NAO PERTURBE (TOQUE P/ SAIR)" : "TOQUE P/ ACORDAR", cx, h - 8);
  } else {
    drawMouth(canvas, cx, mouthY, s);
    updateDateTimeStrip(d);
  }

  if (micActive_ && !physicalMute_) {
    // Glowing neon green mic indicator dot at top-right corner with "MIC ON" label
    d.setTextDatum(textdatum_t::middle_right);
    d.setTextSize(1);
    d.setTextColor(0x07E0); // Vibrant neon green
    d.drawString("MIC ON", w - 24, 14);

    d.fillCircle(w - 14, 14, 6, 0x03E0); // Soft green outer aura
    d.fillCircle(w - 14, 14, 4, 0x07E0); // Vibrant pure green
    d.fillCircle(w - 14, 14, 2, 0x97F4); // Specular center highlight
  }

  if (physicalMute_) {
    // Red mute indicator dot at top-left corner with "MUDO" label
    d.fillCircle(14, 14, 6, 0x8800); // Soft red outer aura
    d.fillCircle(14, 14, 4, 0xF800); // Vibrant red dot
    d.fillCircle(14, 14, 2, 0xFFE0); // Center highlight
    d.setTextDatum(textdatum_t::middle_left);
    d.setTextSize(1);
    d.setTextColor(0xF800);
    d.drawString("MUDO", 24, 14);
  }

  drawQuickStatusDrawer(d);
  uint32_t tFullPush0 = millis();
  canvas.pushSprite(&display, 0, 0);
  uint32_t fullPushMs = millis() - tFullPush0;
  Serial.printf("[DIAG:RENDER] FULL_PUSH_MS=%u\n", fullPushMs);
}

void EmotionController::updateDateTimeStrip(LovyanGFX& d) {
  time_t nowSec = time(nullptr);
  struct tm timeInfo {};
  localtime_r(&nowSec, &timeInfo);
  if (timeInfo.tm_year >= (2024 - 1900)) {
    static const char* const weekdaysPt[] = {
      "domingo", "segunda-feira", "terça-feira", "quarta-feira",
      "quinta-feira", "sexta-feira", "sábado"
    };
    int wday = timeInfo.tm_wday;
    const char* weekday = (wday >= 0 && wday <= 6) ? weekdaysPt[wday] : "";
    char dtBuf[64];
    snprintf(dtBuf, sizeof(dtBuf), "%02d:%02d | %s, %02d/%02d/%04d",
             timeInfo.tm_hour, timeInfo.tm_min, weekday,
             timeInfo.tm_mday, timeInfo.tm_mon + 1, timeInfo.tm_year + 1900);
    d.setTextDatum(textdatum_t::bottom_center);
    d.setTextSize(1);
    d.setTextColor(0x9DF8); // Discreet lavender / light cyan
    d.setFont(&s_latin1Font);
    d.drawString(dtBuf, d.width() / 2, d.height() - 8);
    d.setFont(&fonts::Font0);
  }
}

void EmotionController::renderPartialFace(LovyanGFX& display, const FaceState& s) {
  uint32_t tPartialStart = millis();
  uint32_t leftEyePushMs = 0;
  uint32_t rightEyePushMs = 0;
  uint32_t mouthPushMs = 0;

  bool eyesDirty = (s.pupilDx != lastPupilDx_ || s.pupilDy != lastPupilDy_ || s.closed != lastClosed_);
  bool mouthDirty = false;

  if (s.isSpeaking) {
    if (s.talkShape != lastTalkShape_ || s.talkYOffset != lastTalkYOffset_) {
      mouthDirty = true;
    }
  }

  if (eyesDirty) {
    leftEyeSprite_.fillSprite(TFT_BLACK);
    drawEye(leftEyeSprite_, 48, 44, true, s);
    uint32_t t0 = millis();
    leftEyeSprite_.pushSprite(&display, kLeftEyeSpriteX, kLeftEyeSpriteY);
    leftEyePushMs = millis() - t0;

    rightEyeSprite_.fillSprite(TFT_BLACK);
    drawEye(rightEyeSprite_, 48, 44, false, s);
    t0 = millis();
    rightEyeSprite_.pushSprite(&display, kRightEyeSpriteX, kRightEyeSpriteY);
    rightEyePushMs = millis() - t0;

    lastPupilDx_ = s.pupilDx;
    lastPupilDy_ = s.pupilDy;
    lastClosed_ = s.closed;
  }

  if (mouthDirty) {
    mouthSprite_.fillSprite(TFT_BLACK);
    drawMouth(mouthSprite_, 50, 26, s);
    uint32_t t0 = millis();
    mouthSprite_.pushSprite(&display, kMouthSpriteX, kMouthSpriteY);
    mouthPushMs = millis() - t0;

    lastTalkShape_ = s.talkShape;
    lastTalkYOffset_ = s.talkYOffset;
  }

  uint32_t partialTotalMs = millis() - tPartialStart;
  if (eyesDirty || mouthDirty) {
    Serial.printf("[DIAG:RENDER] PARTIAL total_ms=%u mouth_ms=%u left_eye_ms=%u right_eye_ms=%u eyes_dirty=%d mouth_dirty=%d\n",
                  partialTotalMs, mouthPushMs, leftEyePushMs, rightEyePushMs, eyesDirty ? 1 : 0, mouthDirty ? 1 : 0);
  }
}

void EmotionController::renderFace() {
  if (sleepDisplayOff_) return;
  auto& display = M5StackChan.Display();
  FaceState s = computeFaceState();

  bool fullRedraw = false;
  if (!partialRenderingEnabled_) {
    fullRedraw = true;
  } else if (forceFullRedraw_) {
    fullRedraw = true;
  } else if (s.isSleep) {
    fullRedraw = true;
  } else if (mode_ != lastRenderedMode_) {
    fullRedraw = true;
  } else if (isShaken_ != lastRenderedShaken_ || isLifted_ != lastRenderedLifted_ || isShaken_ || isLifted_) {
    fullRedraw = true;
  } else if (isQuickStatusActive() != lastRenderedQuickStatus_) {
    fullRedraw = true;
  } else if (isQuickStatusActive() && quickStatusDataChanged()) {
    fullRedraw = true;
  } else if (micActive_ != lastRenderedMicActive_ || physicalMute_ != lastRenderedPhysicalMute_) {
    fullRedraw = true;
  } else if (s.isSpeaking != lastRenderedSpeaking_) {
    fullRedraw = true;
  }

  time_t curSec = time(nullptr);
  struct tm curTm {};
  localtime_r(&curSec, &curTm);
  if (curTm.tm_year >= (2024 - 1900) && curTm.tm_min != lastRenderedMinute_) {
    fullRedraw = true;
  }

  if (fullRedraw) {
    renderFullFace(display, s);
    forceFullRedraw_ = false;
    lastRenderedMinute_ = curTm.tm_min;
    lastRenderedMode_ = mode_;
    lastRenderedShaken_ = isShaken_;
    lastRenderedLifted_ = isLifted_;
    lastRenderedQuickStatus_ = isQuickStatusActive();
    lastRenderedMicActive_ = micActive_;
    lastRenderedPhysicalMute_ = physicalMute_;
    lastRenderedSpeaking_ = s.isSpeaking;
    lastPupilDx_ = s.pupilDx;
    lastPupilDy_ = s.pupilDy;
    lastClosed_ = s.closed;
    lastTalkShape_ = s.talkShape;
    lastTalkYOffset_ = s.talkYOffset;
    return;
  }

  renderPartialFace(display, s);
}

bool EmotionController::quickStatusDataChanged() {
  uint32_t now = millis();
  bool changed = false;

  if (iaStatus_ != lastDrawerSnapshot_.iaStatus) {
    changed = true;
  } else if (ipAddress_ != lastDrawerSnapshot_.ip) {
    changed = true;
  } else if (abs(static_cast<int>(batteryPct_) - static_cast<int>(lastDrawerSnapshot_.batPct)) >= 1) {
    changed = true;
  } else if (fabs(currentTemp_ - lastDrawerSnapshot_.temp) >= 0.2f) {
    changed = true;
  } else if (fabs(currentHumidity_ - lastDrawerSnapshot_.hum) >= 1.0f) {
    changed = true;
  } else if (abs(currentTofMm_ - lastDrawerSnapshot_.tofMm) >= 20 && (now - lastDrawerSnapshot_.lastTofRenderMs >= 500)) {
    changed = true;
  }

  if (changed) {
    lastDrawerSnapshot_.iaStatus = iaStatus_;
    lastDrawerSnapshot_.ip = ipAddress_;
    lastDrawerSnapshot_.batPct = batteryPct_;
    lastDrawerSnapshot_.temp = currentTemp_;
    lastDrawerSnapshot_.hum = currentHumidity_;
    if (abs(currentTofMm_ - lastDrawerSnapshot_.tofMm) >= 20) {
      lastDrawerSnapshot_.tofMm = currentTofMm_;
      lastDrawerSnapshot_.lastTofRenderMs = now;
    }
  }

  return changed;
}

void EmotionController::drawQuickStatusDrawer(M5Canvas& d) {
  if (millis() >= quickStatusUntilMs_) return;
  int dw = 304, dh = 46, dx = 8, dy = 4;
  d.fillRoundRect(dx, dy, dw, dh, 8, 0x08C4); // Navy container
  d.drawRoundRect(dx, dy, dw, dh, 8, 0x3DFE); // Glowing cyan border

  // --- Line 1: Wi-Fi RSSI Bars + IP Address + IA Status ---
  uint16_t cActive = 0x3DFE;
  uint16_t cDim = 0x1908;
  int bars = 0;
  if (wifiRssi_ > -60) bars = 4;
  else if (wifiRssi_ > -75) bars = 3;
  else if (wifiRssi_ > -85) bars = 2;
  else if (wifiRssi_ > -100) bars = 1;

  d.fillRect(18, dy + 15, 2, 4, bars >= 1 ? cActive : cDim);
  d.fillRect(22, dy + 12, 2, 7, bars >= 2 ? cActive : cDim);
  d.fillRect(26, dy + 9,  2, 10, bars >= 3 ? cActive : cDim);
  d.fillRect(30, dy + 6,  2, 13, bars >= 4 ? cActive : cDim);

  d.setTextDatum(textdatum_t::top_left);
  d.setTextSize(1);
  d.setTextColor(0x3DFE);
  d.drawString(ipAddress_.length() ? ipAddress_ : "Sem IP", 36, dy + 8);

  d.setTextDatum(textdatum_t::top_right);
  if (iaStatus_ == 2) {
    d.setTextColor(0x07E0); // Pure Green
    d.drawString("● IA ON", dx + dw - 10, dy + 8);
  } else if (iaStatus_ == 1) {
    d.setTextColor(0xFFE0); // Yellow/Amber
    d.drawString("◌ IA ...", dx + dw - 10, dy + 8);
  } else {
    d.setTextColor(0xF800); // Red
    d.drawString("○ IA OFF", dx + dw - 10, dy + 8);
  }

  // --- Line 2: Battery + Temp + Hum + ToF ---
  // 1. Battery: outline + proportional fill + text
  d.drawRect(18, dy + 25, 16, 9, TFT_WHITE);
  d.fillRect(34, dy + 27, 2, 5, TFT_WHITE);
  int fillW = constrain((static_cast<int>(batteryPct_) * 12) / 100, 0, 12);
  uint16_t batCol = batteryPct_ > 20 ? 0x07E0 : 0xF800;
  if (batteryCharging_) batCol = 0xFFE0;
  if (fillW > 0) {
    d.fillRect(20, dy + 27, fillW, 5, batCol);
  }
  d.setTextDatum(textdatum_t::top_left);
  d.setTextColor(batCol);
  d.drawString(String(batteryPct_) + "%", 39, dy + 26);

  // 2. Temperature: mini thermometer + value
  d.fillCircle(80, dy + 33, 3, 0xFD40); // Bulb
  d.fillRect(79, dy + 26, 3, 7, 0xFD40); // Stem
  d.setTextColor(0xFD40);
  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%.1f C", currentTemp_);
  d.drawString(tBuf, 86, dy + 26);

  // 3. Humidity: mini water drop + value
  d.fillCircle(142, dy + 32, 3, 0x3DFE); // Drop body
  d.drawLine(140, dy + 32, 142, dy + 26, 0x3DFE);
  d.drawLine(144, dy + 32, 142, dy + 26, 0x3DFE);
  d.drawPixel(142, dy + 26, 0x3DFE);
  d.setTextColor(0x3DFE);
  char hBuf[16];
  snprintf(hBuf, sizeof(hBuf), "%.0f%%", currentHumidity_);
  d.drawString(hBuf, 148, dy + 26);

  // 4. ToF: arrows <-> + value in cm
  d.drawLine(198, dy + 30, 208, dy + 30, 0xFDA0);
  d.drawLine(198, dy + 30, 201, dy + 28, 0xFDA0);
  d.drawLine(198, dy + 30, 201, dy + 32, 0xFDA0);
  d.drawLine(208, dy + 30, 205, dy + 28, 0xFDA0);
  d.drawLine(208, dy + 30, 205, dy + 32, 0xFDA0);
  d.setTextColor(0xFDA0);
  char distBuf[16];
  if (currentTofMm_ > 0 && currentTofMm_ <= 2000) {
    snprintf(distBuf, sizeof(distBuf), "%dcm", currentTofMm_ / 10);
  } else {
    snprintf(distBuf, sizeof(distBuf), "--");
  }
  d.drawString(distBuf, 212, dy + 26);
}

void EmotionController::renderNeutral() {
  uint8_t v = wave8(frame_ * 2, 4, 18);
  setAll(0, 0, v);
}

void EmotionController::renderListening() {
  uint8_t v = wave8(frame_ * 3, 10, 55);
  setAll(0, scale8(80, v), v);
}

void EmotionController::renderSpeaking() {
  uint8_t v = wave8(frame_ * 10, 25, 95);
  for (uint8_t i = 0; i < kLedCount; ++i) {
    uint8_t local = (i % 2 == 0) ? v : scale8(v, 120);
    setLed(i, scale8(60, local), local, scale8(120, local));
  }
}

void EmotionController::renderThinking() {
  uint8_t head = (frame_ / 2) % kLedCount;
  for (uint8_t i = 0; i < kLedCount; ++i) {
    uint8_t dist = (i + kLedCount - head) % kLedCount;
    uint8_t v = 2;
    if (dist == 0) v = 95;
    else if (dist == 1 || dist == kLedCount - 1) v = 42;
    else if (dist == 2 || dist == kLedCount - 2) v = 14;
    setLed(i, 0, scale8(120, v), v);
  }
}

void EmotionController::renderLooking() {
  // Camera assist light: bright neutral-white LEDs to signal capture/analyze and
  // add a little illumination. No servo/head motion is triggered from this state.
  uint8_t pulse = wave8(frame_ * 4, 120, 210);
  uint8_t sweep = (frame_ / 3) % kLedCount;
  for (uint8_t i = 0; i < kLedCount; ++i) {
    uint8_t v = pulse;
    if (i == sweep || i == (sweep + kLedCount / 2) % kLedCount) v = 245;
    setLed(i, v, v, v);
  }
}

void EmotionController::renderHappy() {
  uint8_t baseHue = frame_ * 3;
  for (uint8_t i = 0; i < kLedCount; ++i) {
    uint8_t r, g, b;
    hsvToRgb(baseHue + i * 21, 230, 85, r, g, b);
    setLed(i, r, g, b);
  }
}

void EmotionController::renderAngry() {
  uint8_t v = wave8(frame_ * 9, 25, 110);
  for (uint8_t i = 0; i < kLedCount; ++i) {
    uint8_t local = (i == ((frame_ / 3) % kLedCount)) ? 130 : v;
    setLed(i, local, 0, 0);
  }
}

void EmotionController::renderFound() {
  uint8_t v = wave8(frame_ * 8, 25, 120);
  for (uint8_t i = 0; i < kLedCount; ++i) {
    if ((i + frame_ / 4) % 3 == 0) setLed(i, v, scale8(220, v), 0);
    else setLed(i, scale8(30, v), scale8(70, v), 0);
  }
}

void EmotionController::renderError() {
  bool on = ((frame_ / 8) % 2) == 0;
  setAll(on ? 110 : 0, on ? 12 : 0, 0);
}

void EmotionController::renderSleep() {
  uint8_t v = wave8(frame_, 0, 14);
  setAll(0, 0, v);
}

void EmotionController::renderMeigo() {
  uint8_t v = wave8(frame_ * 3, 20, 75);
  for (uint8_t i = 0; i < kLedCount; ++i) {
    setLed(i, v, scale8(40, v), scale8(90, v)); // Warm rosy pink
  }
}

void EmotionController::renderScared() {
  uint8_t v = wave8(frame_ * 12, 10, 85);
  setAll(v, scale8(80, v), scale8(20, v)); // Jittery pale amber
}

void EmotionController::renderSurprised() {
  uint8_t v = wave8(frame_ * 8, 30, 110);
  setAll(v, v, scale8(140, v)); // Cyan-white pulse
}

