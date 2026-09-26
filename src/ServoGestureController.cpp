#include "ServoGestureController.h"
#include <M5StackChan.h>

namespace {
// StackChan-BSP units: 10 = 1 degree.
// Production examples use X +/-1000 for 100-degree turns, Y 0..900 for pitch.
// Keep expressive/emotional motions moderate, and expose separate wide look presets.
// User-calibrated neutral pitch: commanding Y=0 sits a little too low on this robot,
// so everyday gestures return to a slightly raised neutral instead of bottoming out.
constexpr int kNeutralY = 250;
// Visual search keeps the camera slightly raised so a horizontal sector scan
// sees more of the room without changing the normal conversational neutral.
constexpr int kSearchY = 180;
ServoGestureController::Step kCenter[] = {
    {0, kNeutralY, 500, 700},
};

ServoGestureController::Step kLookAtUser[] = {
    {0, kNeutralY, 500, 650},
};

ServoGestureController::Step kLookRightSmall[] = {
    {200, kNeutralY, 480, 1800},
    {0, kNeutralY, 400, 650},
};

ServoGestureController::Step kLookLeftSmall[] = {
    {-200, kNeutralY, 480, 1800},
    {0, kNeutralY, 400, 650},
};

// Look Right Normal (90 degrees: turn -> 6s observation -> auto-center deceleration return)
ServoGestureController::Step kLookRightNormal[] = {
    {900, kNeutralY, 480, 6000}, // Reach 90° in ~600ms and observe for ~6s
    {900, kNeutralY, 300, 300},  // 300ms micro-pause before returning
    {350, kNeutralY, 380, 650},  // Deceleration easing (to 35°)
    {0, kNeutralY, 320, 750},    // Soft landing at neutral center
};

// Look Left Normal (90 degrees: turn -> 6s observation -> auto-center deceleration return)
ServoGestureController::Step kLookLeftNormal[] = {
    {-900, kNeutralY, 480, 6000}, // Reach -90° in ~600ms and observe for ~6s
    {-900, kNeutralY, 300, 300},  // 300ms micro-pause before returning
    {-350, kNeutralY, 380, 650},  // Deceleration easing (to -35°)
    {0, kNeutralY, 320, 750},     // Soft landing at neutral center
};

// Glance Right (~25 degrees, short ~2s glance -> return)
ServoGestureController::Step kLookRightGlance[] = {
    {250, kNeutralY, 520, 1400},
    {0, kNeutralY, 450, 600},
};

// Glance Left (~25 degrees, short ~2s glance -> return)
ServoGestureController::Step kLookLeftGlance[] = {
    {-250, kNeutralY, 520, 1400},
    {0, kNeutralY, 450, 600},
};

// Look Right Sustain (90 degrees, permanent hold until center_head)
ServoGestureController::Step kLookRightSustain[] = {
    {900, kNeutralY, 480, 500},
};

// Look Left Sustain (90 degrees, permanent hold until center_head)
ServoGestureController::Step kLookLeftSustain[] = {
    {-900, kNeutralY, 480, 500},
};

ServoGestureController::Step kLookUp[] = {
    {0, 380, 480, 1000},
    {0, kNeutralY, 450, 650},
};

ServoGestureController::Step kLookDown[] = {
    {0, 60, 480, 1000},
    {0, kNeutralY, 450, 650},
};

ServoGestureController::Step kLookTopRight[] = {
    {340, 360, 480, 1000},
    {0, kNeutralY, 450, 650},
};

ServoGestureController::Step kLookTopLeft[] = {
    {-340, 360, 480, 1000},
    {0, kNeutralY, 450, 650},
};

ServoGestureController::Step kSearchLeftWide[] = {
    {-520, kSearchY, 520, 820},
};

ServoGestureController::Step kSearchLeft[] = {
    {-280, kSearchY, 500, 650},
};

ServoGestureController::Step kSearchCenter[] = {
    {0, kSearchY, 500, 620},
};

ServoGestureController::Step kSearchRight[] = {
    {280, kSearchY, 500, 650},
};

ServoGestureController::Step kSearchRightWide[] = {
    {520, kSearchY, 520, 820},
};


ServoGestureController::Step kNodYes[] = {
    {0, 0, 500, 100, true},
    {0, 70, 700, 170, true},
    {0, -40, 700, 170, true},
    {0, 60, 680, 160, true},
    {0, 0, 580, 240, true},
};

ServoGestureController::Step kShakeNo[] = {
    {-160, 0, 600, 160, true},
    {160, 0, 600, 160, true},
    {-100, 0, 560, 150, true},
    {100, 0, 560, 150, true},
    {0, 0, 500, 200, true},
};

ServoGestureController::Step kCuriousTilt[] = {
    {120, 60, 500, 340, true},
    {0, 0, 480, 420, true},
};

ServoGestureController::Step kSpeakingMicro[] = {
    {55, 20, 620, 150, true},
    {-50, 5, 620, 150, true},
    {35, 25, 600, 140, true},
    {0, 0, 580, 180, true},
};

ServoGestureController::Step kIdleBreath[] = {
    {0, 20, 200, 1200, true},
    {0, -10, 180, 1200, true},
    {0, 0, 180, 800, true},
};

// Idle breath calm: amplitude mais suave (+12 / -6 / 0), velocidade reduzida, pausa orgânica
ServoGestureController::Step kIdleBreathCalm[] = {
    {0, 12, 140, 1500, true},
    {0, -6, 120, 1500, true},
    {0, 0, 120, 1000, true},
};

// Idle breath curious: respiracao suave com leve variacao de yaw e retorno estrito ao neutro
ServoGestureController::Step kIdleBreathCurious[] = {
    {30, 18, 180, 1300, true},
    {15, -8, 160, 1300, true},
    {0, 0, 160, 900, true},
};
}  // namespace

void ServoGestureController::begin() {
  stop();
  current_ = "idle";
}

void ServoGestureController::loop() {
  if (!active_) return;
  uint32_t now = millis();
  if (now < nextStepAt_) return;

  if (stepIndex_ >= stepCount_) {
    active_ = false;
    if (updateAnchorOnComplete_) {
      anchorX_ = clampInt(pendingAnchorX_, kMinX, kMaxX);
      anchorY_ = clampInt(pendingAnchorY_, kMinY, kMaxY);
      Serial.printf("ServoGesture: anchor x=%d y=%d\n", anchorX_, anchorY_);
    }
    updateAnchorOnComplete_ = false;
    current_ = "idle";
    return;
  }

  const Step& step = steps_[stepIndex_++];
  issueStep(step);
  nextStepAt_ = now + step.holdMs;
}

bool ServoGestureController::queueGesture(const String& name, const String& hold) {
  if (name == "center_head" || name == "center" || name == "home") {
    Serial.printf("[DIAG:PITCH] CENTER_HEAD requestedY=%d anchorY=%d\n", kNeutralY, anchorY_);
    start("center_head", kCenter, sizeof(kCenter) / sizeof(kCenter[0]), true);
    return true;
  }
  if (name == "look_at_user") {
    start("look_at_user", kLookAtUser, sizeof(kLookAtUser) / sizeof(kLookAtUser[0]), true);
    return true;
  }
  if (name == "look_right_small") {
    start("look_right_small", kLookRightSmall, sizeof(kLookRightSmall) / sizeof(kLookRightSmall[0]), true);
    return true;
  }
  if (name == "look_left_small") {
    start("look_left_small", kLookLeftSmall, sizeof(kLookLeftSmall) / sizeof(kLookLeftSmall[0]), true);
    return true;
  }
  if (name == "look_right") {
    if (hold == "glance") {
      start("look_right", kLookRightGlance, sizeof(kLookRightGlance) / sizeof(kLookRightGlance[0]), false);
    } else if (hold == "sustain") {
      start("look_right", kLookRightSustain, sizeof(kLookRightSustain) / sizeof(kLookRightSustain[0]), true);
    } else { // "normal" (default)
      start("look_right", kLookRightNormal, sizeof(kLookRightNormal) / sizeof(kLookRightNormal[0]), false);
    }
    return true;
  }
  if (name == "look_left") {
    if (hold == "glance") {
      start("look_left", kLookLeftGlance, sizeof(kLookLeftGlance) / sizeof(kLookLeftGlance[0]), false);
    } else if (hold == "sustain") {
      start("look_left", kLookLeftSustain, sizeof(kLookLeftSustain) / sizeof(kLookLeftSustain[0]), true);
    } else { // "normal" (default)
      start("look_left", kLookLeftNormal, sizeof(kLookLeftNormal) / sizeof(kLookLeftNormal[0]), false);
    }
    return true;
  }
  if (name == "look_up") {
    start("look_up", kLookUp, sizeof(kLookUp) / sizeof(kLookUp[0]), true);
    return true;
  }
  if (name == "look_down") {
    start("look_down", kLookDown, sizeof(kLookDown) / sizeof(kLookDown[0]), true);
    return true;
  }
  if (name == "look_top_right") {
    start("look_top_right", kLookTopRight, sizeof(kLookTopRight) / sizeof(kLookTopRight[0]), true);
    return true;
  }
  if (name == "look_top_left") {
    start("look_top_left", kLookTopLeft, sizeof(kLookTopLeft) / sizeof(kLookTopLeft[0]), true);
    return true;
  }
  if (name == "search_left_wide") {
    start("search_left_wide", kSearchLeftWide, sizeof(kSearchLeftWide) / sizeof(kSearchLeftWide[0]), true);
    return true;
  }
  if (name == "search_left") {
    start("search_left", kSearchLeft, sizeof(kSearchLeft) / sizeof(kSearchLeft[0]), true);
    return true;
  }
  if (name == "search_center") {
    start("search_center", kSearchCenter, sizeof(kSearchCenter) / sizeof(kSearchCenter[0]), true);
    return true;
  }
  if (name == "search_right") {
    start("search_right", kSearchRight, sizeof(kSearchRight) / sizeof(kSearchRight[0]), true);
    return true;
  }
  if (name == "search_right_wide") {
    start("search_right_wide", kSearchRightWide, sizeof(kSearchRightWide) / sizeof(kSearchRightWide[0]), true);
    return true;
  }
  if (name == "nod_yes") {
    start("nod_yes", kNodYes, sizeof(kNodYes) / sizeof(kNodYes[0]));
    return true;
  }
  if (name == "shake_no") {
    start("shake_no", kShakeNo, sizeof(kShakeNo) / sizeof(kShakeNo[0]));
    return true;
  }
  if (name == "curious_tilt") {
    start("curious_tilt", kCuriousTilt, sizeof(kCuriousTilt) / sizeof(kCuriousTilt[0]));
    return true;
  }
  if (name == "speaking_micro_motion") {
    start("speaking_micro_motion", kSpeakingMicro, sizeof(kSpeakingMicro) / sizeof(kSpeakingMicro[0]));
    return true;
  }
  if (name == "idle_breath") {
    start("idle_breath", kIdleBreath, sizeof(kIdleBreath) / sizeof(kIdleBreath[0]));
    return true;
  }
  if (name == "idle_breath_calm") {
    start("idle_breath_calm", kIdleBreathCalm, sizeof(kIdleBreathCalm) / sizeof(kIdleBreathCalm[0]));
    return true;
  }
  if (name == "idle_breath_curious") {
    start("idle_breath_curious", kIdleBreathCurious, sizeof(kIdleBreathCurious) / sizeof(kIdleBreathCurious[0]));
    return true;
  }
  if (name == "stop_motion" || name == "stop") {
    stop();
    return true;
  }
  return false;
}

bool ServoGestureController::queueSteps(const String& name, const Step* steps, uint8_t count) {
  if (!steps || count == 0) return false;
  // Parametric motions are interpreted relative to the current persistent look
  // anchor. The final keyframe becomes the new anchor, so a deliberate "look a
  // little left" survives later nods/tilts/speaking micro-motions.
  start(name.length() ? name : String("custom_motion"), steps, count, true);
  return true;
}

void ServoGestureController::stop() {
  active_ = false;
  stepCount_ = 0;
  stepIndex_ = 0;
  nextStepAt_ = 0;
  current_ = "idle";
  M5StackChan.Motion.stop();
}

void ServoGestureController::start(const String& name, const Step* steps, uint8_t count, bool updateAnchorOnComplete) {
  if (count > kMaxSteps) count = kMaxSteps;
  for (uint8_t i = 0; i < count; ++i) {
    steps_[i] = steps[i];
  }
  stepCount_ = count;
  stepIndex_ = 0;
  current_ = name;
  active_ = true;
  nextStepAt_ = 0;
  updateAnchorOnComplete_ = updateAnchorOnComplete;
  if (updateAnchorOnComplete_ && count > 0) {
    const Step& last = steps_[count - 1];
    pendingAnchorX_ = last.relative ? anchorX_ + last.x : last.x;
    pendingAnchorY_ = last.relative ? anchorY_ + last.y : last.y;
  }
  Serial.printf("ServoGesture: queued %s steps=%u anchor=%d,%d update_anchor=%s\n",
                name.c_str(), static_cast<unsigned>(count), anchorX_, anchorY_,
                updateAnchorOnComplete_ ? "yes" : "no");
}

void ServoGestureController::issueStep(const Step& step) {
  int rawX = step.relative ? anchorX_ + step.x : step.x;
  int rawY = step.relative ? anchorY_ + step.y : step.y;
  int x = clampInt(rawX, kMinX, kMaxX);
  int y = clampInt(rawY, kMinY, kMaxY);
  int speed = clampInt(step.speed, kMinSpeed, kMaxSpeed);
  Serial.printf("ServoGesture: step x=%d y=%d speed=%d hold=%u relative=%s\n",
                x, y, speed, static_cast<unsigned>(step.holdMs), step.relative ? "yes" : "no");
  Serial.printf("[DIAG:PITCH] ISSUE_STEP anchorY=%d stepY=%d rawY=%d finalY=%d speed=%d relative=%s\n",
                anchorY_, step.y, rawY, y, speed, step.relative ? "yes" : "no");
  Serial.printf("[DIAG:PITCH] BSP PITCH COMMAND y=%d speed=%d\n", y, speed);
  M5StackChan.Motion.move(x, y, speed);
}

int ServoGestureController::clampInt(int value, int lo, int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}
