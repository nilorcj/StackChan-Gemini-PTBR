#pragma once

#include <Arduino.h>

// Non-blocking StackChan head gesture controller using the production BSP angle units.
// BSP units: 10 = 1 degree; yaw X -1280..1280, pitch Y 0..900.
// Public methods only queue gestures and return immediately; loop() executes steps.
class ServoGestureController {
 public:
  struct Step {
    int x;
    int y;
    int speed;
    uint16_t holdMs;
    bool relative = false;
  };

  void begin();
  void loop();

  bool queueGesture(const String& name, const String& hold = "normal");
  bool queueSteps(const String& name, const Step* steps, uint8_t count);
  void stop();

  const String& currentGesture() const { return current_; }
  bool active() const { return active_; }
  int anchorX() const { return anchorX_; }
  int anchorY() const { return anchorY_; }

 private:

  static constexpr int kNeutralX = 0;
  static constexpr int kNeutralY = 250;
  static constexpr int kMinX = -1280;
  static constexpr int kMaxX = 1280;
  static constexpr int kMinY = 50;
  static constexpr int kMaxY = 850;
  static constexpr int kMinSpeed = 0;
  static constexpr int kMaxSpeed = 1000;
  static constexpr uint8_t kMaxSteps = 16;

  Step steps_[kMaxSteps]{};
  uint8_t stepCount_ = 0;
  uint8_t stepIndex_ = 0;
  uint32_t nextStepAt_ = 0;
  bool active_ = false;
  String current_ = "idle";
  int anchorX_ = kNeutralX;
  int anchorY_ = kNeutralY;
  bool updateAnchorOnComplete_ = false;
  int pendingAnchorX_ = kNeutralX;
  int pendingAnchorY_ = kNeutralY;

  void start(const String& name, const Step* steps, uint8_t count, bool updateAnchorOnComplete = false);
  void issueStep(const Step& step);
  static int clampInt(int value, int lo, int hi);
};
