#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <M5UnitENV.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedTOF.h>

/**
 * @brief M5Stack Unit PaHUB (I2C Multiplexer PCA9548AP / TCA9548A) Manager
 * 
 * Expands a single I2C Port A into 8 isolated I2C channels (0 to 7).
 * Standard addresses:
 *  - PaHUB: 0x77 (address pins configured for 0x77)
 *  - Unit TOF 90 (VL53L0X): 0x29 on Channel 0
 *  - Unit ENV (SHT30: 0x44, QMP6988: 0x70) on Channel 1
 *  - Unit Vibrator: Connected via GPIO pin or PWM output
 */
class UnitPaHubManager {
 public:
  static constexpr uint8_t kPaHubDefaultI2CAddr = 0x77; // PaHub configured to 0x77
  static constexpr uint8_t kPaHubAltI2CAddr = 0x70;     // Legacy/alternate address
  static constexpr uint8_t kDefaultVibratorPin = 9;     // CoreS3 Port B Pin 1 Yellow (DIN on U059)
  static constexpr uint8_t kDefaultVibratorPin2 = 8;    // CoreS3 Port B Pin 2 White (NC on U059)
  
  // PaHub Channels Assignment
  enum Channel : uint8_t {
    CH_TOF_90 = 0,   // Channel 0: Laser Distance Sensor TOF 90 (VL53L0X / VL53L1X 0x29)
    CH_ENV = 1,      // Channel 1: Temperature / Humidity / Pressure (SHT30: 0x44, QMP6988: 0x70)
    CH_SPARE_2 = 2,  // Channel 2: Aux / Unit Vibrator Driver
    CH_SPARE_3 = 3,
    CH_SPARE_4 = 4,
    CH_SPARE_5 = 5,
    CH_SPARE_6 = 6,
    CH_SPARE_7 = 7
  };

  enum class HapticPattern : uint8_t {
    None,
    Click,
    Purr,
    Alert,
    Angry,
    CatPurr,
    Shake,
    Lift,
    Relief
  };

  UnitPaHubManager(uint8_t i2cAddr = kPaHubDefaultI2CAddr, uint8_t vibratorPin = kDefaultVibratorPin)
      : pahubAddr_(i2cAddr), vibratorPin_(vibratorPin) {}

  bool begin(TwoWire& wire = Wire) {
    wire_ = &wire;
    wire_->begin(2, 1, 100000);
    wire_->setTimeOut(50); // Prevent I2C clock stretching from triggering WDT reboots
    
    // Auto-detect PaHub multiplexer at 0x77 (configured address) first
    uint8_t probeAddrs[] = {0x77, pahubAddr_, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76};
    pahubFound_ = false;
    for (uint8_t addr : probeAddrs) {
      wire_->beginTransmission(addr);
      if (wire_->endTransmission() == 0) {
        pahubAddr_ = addr;
        pahubFound_ = true;
        Serial.printf("UnitPaHub: PCA9548AP/TCA9548A auto-detected at 0x%02X\n", pahubAddr_);
        break;
      }
    }

    if (!pahubFound_) {
      Serial.println("UnitPaHub: Multiplexer not detected on I2C bus");
    }
    
    // Setup Unit Vibrator U059 on Port B (Yellow DIN = GPIO 9) with 1kHz motor PWM (official M5 spec)
    ledcSetup(kVibratorPwmChannel1, kVibratorPwmFreq, kVibratorPwmResolution);
    ledcAttachPin(vibratorPin_, kVibratorPwmChannel1);
    ledcWrite(kVibratorPwmChannel1, 0);
    pinMode(kDefaultVibratorPin2, INPUT);
    Serial.printf("UnitVibrator: official 1kHz PWM initialized on Port B Yellow (GPIO %u, DIN) for U059 motor\n",
                  vibratorPin_);

    if (!pahubFound_) return false;

    // Probe TOF sensor on Channel 0: auto-detect VL53L0X or VL53L1X (TOF90)
    if (!selectChannel(CH_TOF_90)) return false;
    tofReady_ = false;
    if (tofUnits0_.add(tof0_, *wire_) && tofUnits0_.begin()) {
      tofReady_ = true;
      isVL53L1X_ = false;
      Serial.println("UnitPaHub: TOF detected as VL53L0X");
    } else if (tofUnits1_.add(tof1_, *wire_) && tofUnits1_.begin()) {
      tofReady_ = true;
      isVL53L1X_ = true;
      tof1_.startPeriodicMeasurement();
      Serial.println("UnitPaHub: TOF detected as VL53L1X (TOF90)");
    } else {
      Serial.println("UnitPaHub: TOF not detected on Ch 0");
    }
    disableAllChannels();

    if (!selectChannel(CH_ENV)) return false;
    bool envOk = sht_.begin(wire_, SHT3X_I2C_ADDR, 2, 1, 100000U);
    envOk = qmp_.begin(wire_, QMP6988_SLAVE_ADDRESS_L, 2, 1, 100000U) && envOk;
    envReady_ = envOk;
    disableAllChannels();
    Serial.printf("UnitPaHub: drivers tof=%s env=%s\n", tofReady_ ? "ok" : "failed", envOk ? "ok" : "failed");
    return tofReady_ || envOk;
  }

  bool selectChannel(uint8_t channel) {
    if (!pahubFound_ || !wire_ || channel > 7) return false;
    wire_->beginTransmission(pahubAddr_);
    wire_->write(1 << channel); // Bitmask: 1 << ch
    return (wire_->endTransmission() == 0);
  }

  void scanChannel(uint8_t channel) {
    if (!selectChannel(channel)) {
      Serial.printf("UnitPaHub: channel %u unavailable\n", channel);
      return;
    }
    Serial.printf("UnitPaHub: scan channel %u:", channel);
    for (uint8_t address = 1; address < 0x78; ++address) {
      wire_->beginTransmission(address);
      if (wire_->endTransmission() == 0) {
        Serial.printf(" 0x%02X", address);
      }
    }
    Serial.println();
    disableAllChannels();
  }

  void disableAllChannels() {
    if (!pahubFound_ || !wire_) return;
    wire_->beginTransmission(pahubAddr_);
    wire_->write(0x00);
    wire_->endTransmission();
  }

  // --- HAPTIC VIBRATION MOTOR CONTROL (U059 DC ERM MOTOR) ---
  static constexpr uint8_t kVibratorPwmChannel1 = 6;
  static constexpr uint32_t kVibratorPwmFreq = 1000; // 1 kHz standard motor PWM (official M5 U059 spec)
  static constexpr uint8_t kVibratorPwmResolution = 8;
  static constexpr uint8_t kDutyMax = 70;         // Subtle ceiling (~27% PWM max, minimal spec)
  static constexpr uint8_t kDutyDefault = 45;     // Gentle minimum tactile tickle (~17% PWM)

  void setVibrator(bool active, uint8_t strength = kDutyDefault) {
    if (!active || strength == 0 || vibrationScale_ == 0) {
      ledcWrite(kVibratorPwmChannel1, 0);
      if (vibratorActive_) lastVibratorActiveMs_ = millis();
      vibratorActive_ = false;
      return;
    }
    uint16_t scaled = (static_cast<uint16_t>(strength) * vibrationScale_) / 100;
    uint8_t duty = (scaled > kDutyMax) ? kDutyMax : static_cast<uint8_t>(scaled);
    ledcWrite(kVibratorPwmChannel1, duty);
    vibratorActive_ = true;
    lastVibratorActiveMs_ = millis();
  }

  bool isVibrating() const {
    return vibratorActive_ || (millis() - lastVibratorActiveMs_ < 200);
  }

  void setVibrationIntensity(uint8_t percent) {
    vibrationScale_ = (percent > 100) ? 100 : percent;
  }
  uint8_t getVibrationIntensity() const { return vibrationScale_; }

  void testBuzz(uint16_t durationMs = 30) {
    setVibrator(true, kDutyDefault);
    delay(durationMs);
    setVibrator(false);
  }

  // Non-blocking tick called inside main loop
  void tick() {
    uint32_t now = millis();
    if (hapticPattern_ == HapticPattern::None) {
      if (vibratorActive_) setVibrator(false);
      return;
    }

    uint32_t elapsed = now - patternStartMs_;

    if (hapticPattern_ == HapticPattern::Click) {
      // Subtle micro-click (~16% PWM)
      if (elapsed < 30) {
        setVibrator(true, 42);
      } else {
        stopHaptic();
      }
    } else if (hapticPattern_ == HapticPattern::Purr) {
      // Very soft gentle purr (~15% PWM)
      if (elapsed < 600) {
        uint16_t phase = elapsed % 160;
        if (phase < 45) {
          setVibrator(true, 38);
        } else {
          setVibrator(false);
        }
      } else {
        stopHaptic();
      }
    } else if (hapticPattern_ == HapticPattern::Alert) {
      // Two subtle micro-taps (~19% PWM)
      if (elapsed < 40) {
        setVibrator(true, 50);
      } else if (elapsed < 90) {
        setVibrator(false);
      } else if (elapsed < 130) {
        setVibrator(true, 50);
      } else {
        stopHaptic();
      }
    } else if (hapticPattern_ == HapticPattern::Angry || hapticPattern_ == HapticPattern::Shake) {
      // Subtle double pulse when shaken (~25% PWM)
      if (elapsed < 80) {
        setVibrator(true, 65);
      } else if (elapsed < 130) {
        setVibrator(false);
      } else if (elapsed < 210) {
        setVibrator(true, 65);
      } else {
        stopHaptic();
      }
    } else if (hapticPattern_ == HapticPattern::Lift) {
      // Gentle startled double-pulse on lift (~21% PWM)
      if (elapsed < 50) {
        setVibrator(true, 55);
      } else if (elapsed < 100) {
        setVibrator(false);
      } else if (elapsed < 150) {
        setVibrator(true, 50);
      } else {
        stopHaptic();
      }
    } else if (hapticPattern_ == HapticPattern::Relief) {
      // Very gentle settling tap upon returning to table (~15% PWM)
      if (elapsed < 35) {
        setVibrator(true, 38);
      } else {
        stopHaptic();
      }
    } else if (hapticPattern_ == HapticPattern::CatPurr) {
      // Gentle docile rhythmic cat purr (~15% PWM)
      uint16_t phase = elapsed % 180;
      if (phase < 45) {
        setVibrator(true, 38);
      } else {
        setVibrator(false);
      }
    }
  }

  void click() {
    hapticPattern_ = HapticPattern::Click;
    patternStartMs_ = millis();
  }

  void purr() {
    hapticPattern_ = HapticPattern::Purr;
    patternStartMs_ = millis();
  }

  void catPurr() {
    hapticPattern_ = HapticPattern::CatPurr;
    patternStartMs_ = millis();
  }

  void buzzAlert() {
    hapticPattern_ = HapticPattern::Alert;
    patternStartMs_ = millis();
  }

  void shiverAngry() {
    hapticPattern_ = HapticPattern::Angry;
    patternStartMs_ = millis();
  }

  void vibrateShake() {
    hapticPattern_ = HapticPattern::Shake;
    patternStartMs_ = millis();
  }

  void vibrateLift() {
    hapticPattern_ = HapticPattern::Lift;
    patternStartMs_ = millis();
  }

  void vibrateRelief() {
    hapticPattern_ = HapticPattern::Relief;
    patternStartMs_ = millis();
  }

  void stopHaptic() {
    hapticPattern_ = HapticPattern::None;
    setVibrator(false);
  }

  bool isTOFReady() const { return tofReady_; }
  bool isENVReady() const { return envReady_; }

  // --- UNIT TOF 90 DISTANCE SENSING ---
  uint16_t readDistanceTof90Mm() {
    if (!tofReady_ || !selectChannel(CH_TOF_90)) return 0;
    if (isVL53L1X_) {
      tofUnits1_.update();
    } else {
      tofUnits0_.update();
    }
    int16_t range = isVL53L1X_ ? tof1_.range() : tof0_.range();
    uint16_t dist = (range > 0) ? static_cast<uint16_t>(range) : 0;
    disableAllChannels();
    return dist;
  }

  // --- UNIT ENV TEMPERATURE & HUMIDITY ---
  struct EnvData {
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;
    bool valid = false;
  };

  EnvData readEnvData() {
    EnvData data;
    if (!selectChannel(CH_ENV)) return data;
    bool shtOk = sht_.update();
    bool qmpOk = qmp_.update();
    if (shtOk) {
      data.temperature = sht_.cTemp;
      data.humidity = sht_.humidity;
    }
    if (qmpOk) data.pressure = qmp_.pressure / 100.0f;
    data.valid = shtOk || qmpOk;
    disableAllChannels();
    return data;
  }

  bool isFound() const { return pahubFound_; }
  uint8_t getAddress() const { return pahubAddr_; }

  bool readSensors(uint16_t& distanceMm, EnvData& env) {
    distanceMm = readDistanceTof90Mm();
    env = readEnvData();
    return distanceMm != 0 || env.valid;
  }

 private:
  TwoWire* wire_ = nullptr;
  uint8_t pahubAddr_ = kPaHubDefaultI2CAddr;
  uint8_t vibratorPin_ = kDefaultVibratorPin;
  bool pahubFound_ = false;
  bool vibratorActive_ = false;
  uint32_t lastVibratorActiveMs_ = 0;
  uint8_t vibrationScale_ = 100;
  HapticPattern hapticPattern_ = HapticPattern::None;
  uint32_t patternStartMs_ = 0;
  m5::unit::UnitUnified tofUnits0_;
  m5::unit::UnitUnified tofUnits1_;
  m5::unit::UnitVL53L0X tof0_;
  m5::unit::UnitVL53L1X tof1_;
  bool isVL53L1X_ = false;
  bool tofReady_ = false;
  bool envReady_ = false;
  SHT3X sht_;
  QMP6988 qmp_;
};
