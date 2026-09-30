#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>

class GeminiLiveProbe;
class GoogleCalendarClient;

class LiveFeedsManager {
 public:
  struct WeatherData {
    char city[32] = "Brasilia - DF";
    float temp = 31.0f;
    float humidity = 25.0f;
    float wind = 14.0f;
    char updatedAt[8] = "12:00";
    bool valid = false;
  };

  struct DreamCardData {
    const char* tag;
    const char* title;
    const char* detail;
  };

  struct NewsItem {
    char category[16] = {0};
    char title[128] = {0};
    char summary[224] = {0};
    char link[128] = {0};
  };

  static constexpr uint8_t kMaxCachedNews = 8;

  struct LiveFeedsSnapshot {
    WeatherData weather;
    char usdBrl[8] = "5.18";
    char currentNewsHeadline[128] = {0};
    NewsItem cachedNews[kMaxCachedNews];
    uint8_t cachedNewsCount = 0;
    uint32_t lastWeatherUpdatedMs = 0;
    uint32_t lastUsdUpdatedMs = 0;
    uint32_t lastNewsUpdatedMs = 0;
    bool valid = false;
  };

  enum class CycleStep : uint8_t {
    Idle = 0,
    PendingNews,
    PendingWeather,
    PendingUsd,
    Publishing
  };

  enum class WorkerState : uint8_t {
    Idle = 0,
    Running,
    WaitingIdle
  };

  static constexpr uint32_t kBootPrefetchTimeoutMs = 8000; // 8.0s total boot prefetch timeout

  LiveFeedsManager();
  ~LiveFeedsManager();

  void begin();
  void loop();

  void setGeminiLiveProbe(const GeminiLiveProbe* gemini) { gemini_ = gemini; }
  void setGoogleCalendar(GoogleCalendarClient* cal) { calendar_ = cal; }
  void setSystemIdle(bool idle) { systemIdle_.store(idle, std::memory_order_release); }

  void triggerBootPrefetch();
  bool waitForBootPrefetch(uint32_t timeoutMs = kBootPrefetchTimeoutMs);
  bool isBootPrefetchCompleted() const { return bootPrefetchCompleted_.load(std::memory_order_acquire); }
  bool isBootPrefetchAborted() const { return bootPrefetchAborted_.load(std::memory_order_acquire); }
  bool isRequestInFlight() const { return requestInFlight_.load(std::memory_order_acquire); }
  bool isCalendarJobInFlight() const { return calendarJobInFlight_.load(std::memory_order_acquire); }

  void requestRefresh();
  void forceRefresh();
  void requestCalendarRefresh();

  // Thread-safe copy-out getters (no heap allocation, zero String under mutex)
  LiveFeedsSnapshot getSnapshot() const;
  WeatherData weather() const;
  String usdBrl() const;
  String latestNews() const;
  uint8_t cachedNewsCount() const;
  DreamCardData getDreamCard(uint8_t index) const;
  uint8_t dreamCount() const { return 15; }

  // Diagnostics (cross-core thread-safe atomic reads)
  uint32_t getStackHighWaterMark() const;
  WorkerState workerState() const { return state_.load(std::memory_order_acquire); }
  CycleStep cycleStep() const { return step_.load(std::memory_order_acquire); }
  bool isUpdating() const { return state_.load(std::memory_order_acquire) != WorkerState::Idle; }

 private:
  static constexpr uint32_t kRefreshIntervalMs = 15 * 60 * 1000UL; // 15 minutes
  static constexpr uint32_t kHttpTimeoutMs = 3500;                // 3500 ms (3.5s) HTTP timeout

  // Cross-core / FreeRTOS primitives
  TaskHandle_t workerTaskHandle_ = nullptr;
  SemaphoreHandle_t snapshotMutex_ = nullptr;

  const GeminiLiveProbe* gemini_ = nullptr;
  std::atomic<bool> systemIdle_{false};
  std::atomic<bool> refreshRequested_{false};
  std::atomic<bool> bootPrefetchActive_{false};
  std::atomic<bool> bootPrefetchCompleted_{false};
  std::atomic<bool> bootPrefetchAborted_{false};
  std::atomic<bool> bootPrefetchAbortRequested_{false};
  std::atomic<bool> requestInFlight_{false};
  std::atomic<uint32_t> bootPrefetchStartMs_{0};

  // State machine on Core 0 (cross-core atomic access)
  std::atomic<CycleStep> step_{CycleStep::Idle};
  std::atomic<WorkerState> state_{WorkerState::Idle};
  uint32_t bootMs_ = 0;
  uint32_t lastRefreshCycleMs_ = 0;

  // Shared snapshot (written by Core 0 worker under mutex, read by Core 1 readers under mutex)
  LiveFeedsSnapshot publishedSnapshot_;

  // Worker-private local snapshot (modified without mutex during sequential steps)
  LiveFeedsSnapshot localSnapshot_;

  static void workerTaskThunk(void* param);
  void workerTaskLoop();

  GoogleCalendarClient* calendar_ = nullptr;
  std::atomic<bool> calendarRefreshRequested_{false};
  std::atomic<bool> calendarJobInFlight_{false};
  uint32_t lastCalendarFetchMs_ = 0;
  uint32_t lastCalendarAttemptMs_ = 0;
  uint32_t calendarNextRetryMs_ = 0;
  static constexpr uint32_t kCalendarRefreshIntervalMs = 30 * 60 * 1000UL; // 30 minutes
  static constexpr uint32_t kCalendarFailureBackoffMs = 60 * 1000UL;      // 60 s backoff

  bool canStartCalendarFetch() const;
  bool canContinueCalendarFetch() const;
  void executeCalendarFetch();

  bool isBootDeadlineExpired() const;
  void abortBootPrefetchAndPublish(const char* reason);
  bool canStartRequest() const;
  void fetchWeather(WeatherData& outWeather);
  void fetchCurrency(char* outUsdBrl);
  void fetchNews(NewsItem* outNews, uint8_t& outCount, char* outHeadline);
};
