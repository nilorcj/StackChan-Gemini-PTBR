#pragma once

#include <Arduino.h>
#include <atomic>

enum class TlsOwner : uint8_t {
  NONE = 0,
  GEMINI,
  LIVEFEEDS
};

class TlsArbiter {
public:
  static TlsArbiter& instance() {
    static TlsArbiter s_instance;
    return s_instance;
  }

  // Request priority for Gemini immediately on wake/connect
  void requestGeminiPriority() {
    gemini_priority_pending_.store(true, std::memory_order_release);
  }

  // Gemini acquires TLS exclusively. Returns true if acquired.
  bool acquireGemini() {
    gemini_priority_pending_.store(true, std::memory_order_release);
    TlsOwner expected = TlsOwner::NONE;
    if (owner_.compare_exchange_strong(expected, TlsOwner::GEMINI, std::memory_order_acq_rel)) {
      gemini_priority_pending_.store(false, std::memory_order_release);
      Serial.println("[TLS:GATE] Gemini acquired TLS");
      return true;
    }
    if (expected == TlsOwner::GEMINI) {
      gemini_priority_pending_.store(false, std::memory_order_release);
      return true;
    }
    // Owner is LIVEFEEDS
    Serial.println("[TLS:GATE] Gemini acquire deferred: owner=LIVEFEEDS pending=1");
    return false;
  }

  // Forcibly claim TLS for Gemini if LiveFeeds takes too long
  void forceTakeoverGemini() {
    feeds_ref_count_.store(0, std::memory_order_release);
    owner_.store(TlsOwner::GEMINI, std::memory_order_release);
    gemini_priority_pending_.store(false, std::memory_order_release);
    Serial.println("[TLS:GATE] Gemini forced TLS takeover");
  }

  // Release Gemini ownership. Only called when disconnected, socket clean, state DOWN.
  void releaseGemini() {
    TlsOwner expected = TlsOwner::GEMINI;
    if (owner_.compare_exchange_strong(expected, TlsOwner::NONE, std::memory_order_acq_rel)) {
      Serial.println("[TLS:GATE] Gemini released TLS -> NONE");
    }
    gemini_priority_pending_.store(false, std::memory_order_release);
  }

  // LiveFeeds attempts to acquire TLS non-blocking.
  bool tryAcquireFeeds() {
    if (gemini_priority_pending_.load(std::memory_order_acquire)) {
      Serial.println("[TLS:GATE] feeds deferred owner=GEMINI");
      return false;
    }
    TlsOwner expected = TlsOwner::NONE;
    if (owner_.compare_exchange_strong(expected, TlsOwner::LIVEFEEDS, std::memory_order_acq_rel)) {
      if (gemini_priority_pending_.load(std::memory_order_acquire)) {
        owner_.store(TlsOwner::NONE, std::memory_order_release);
        Serial.println("[TLS:GATE] feeds deferred owner=GEMINI");
        return false;
      }
      feeds_ref_count_.store(1, std::memory_order_release);
      return true;
    }
    if (expected == TlsOwner::LIVEFEEDS) {
      feeds_ref_count_.fetch_add(1, std::memory_order_acq_rel);
      return true;
    }
    Serial.println("[TLS:GATE] feeds deferred owner=GEMINI");
    return false;
  }

  // Release LiveFeeds ownership
  void releaseFeeds() {
    if (owner_.load(std::memory_order_acquire) == TlsOwner::LIVEFEEDS) {
      int prev = feeds_ref_count_.fetch_sub(1, std::memory_order_acq_rel);
      if (prev <= 1) {
        feeds_ref_count_.store(0, std::memory_order_release);
        owner_.store(TlsOwner::NONE, std::memory_order_release);
      }
    }
  }

  // Status queries
  TlsOwner getOwner() const {
    return owner_.load(std::memory_order_acquire);
  }

  bool isGeminiPriorityPending() const {
    return gemini_priority_pending_.load(std::memory_order_acquire);
  }

  bool isGeminiActive() const {
    return owner_.load(std::memory_order_acquire) == TlsOwner::GEMINI ||
           gemini_priority_pending_.load(std::memory_order_acquire);
  }

private:
  TlsArbiter() = default;
  std::atomic<TlsOwner> owner_{TlsOwner::NONE};
  std::atomic<bool> gemini_priority_pending_{false};
  std::atomic<int> feeds_ref_count_{0};
};

// RAII guard for LiveFeeds operations
class TlsFeedsGuard {
public:
  TlsFeedsGuard() : acquired_(TlsArbiter::instance().tryAcquireFeeds()) {}
  ~TlsFeedsGuard() {
    if (acquired_) {
      TlsArbiter::instance().releaseFeeds();
      acquired_ = false;
    }
  }
  bool isAcquired() const { return acquired_; }
  explicit operator bool() const { return acquired_; }

  TlsFeedsGuard(const TlsFeedsGuard&) = delete;
  TlsFeedsGuard& operator=(const TlsFeedsGuard&) = delete;

private:
  bool acquired_ = false;
};

