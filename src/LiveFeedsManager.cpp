#include "LiveFeedsManager.h"
#include "GeminiLiveProbe.h"
#include "GoogleCalendarClient.h"
#include "TlsArbiter.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sntp.h>

namespace {
struct RequestInFlightGuard {
  std::atomic<bool>& flag;
  explicit RequestInFlightGuard(std::atomic<bool>& f) : flag(f) {
    flag.store(true, std::memory_order_release);
  }
  ~RequestInFlightGuard() {
    flag.store(false, std::memory_order_release);
  }
};
}  // namespace

LiveFeedsManager::LiveFeedsManager() {
  bootMs_ = millis();
}

LiveFeedsManager::~LiveFeedsManager() {
  if (workerTaskHandle_) {
    vTaskDelete(workerTaskHandle_);
    workerTaskHandle_ = nullptr;
  }
  if (snapshotMutex_) {
    vSemaphoreDelete(snapshotMutex_);
    snapshotMutex_ = nullptr;
  }
}

void LiveFeedsManager::begin() {
  if (!snapshotMutex_) {
    snapshotMutex_ = xSemaphoreCreateMutex();
  }
  bootMs_ = millis();

  if (!workerTaskHandle_) {
    xTaskCreatePinnedToCore(
      &LiveFeedsManager::workerTaskThunk,
      "LiveFeedsWorker",
      16384,          // 16 KB stack in bytes
      this,
      1,              // Priority 1
      &workerTaskHandle_,
      0               // Core 0
    );
    Serial.println("[LiveFeeds] Worker task created on Core 0, priority 1, stack 16KB.");
  }
}

void LiveFeedsManager::triggerBootPrefetch() {
  bootPrefetchStartMs_.store(millis(), std::memory_order_release);
  bootPrefetchCompleted_.store(false, std::memory_order_release);
  bootPrefetchAborted_.store(false, std::memory_order_release);
  bootPrefetchAbortRequested_.store(false, std::memory_order_release);
  requestInFlight_.store(false, std::memory_order_release);
  bootPrefetchActive_.store(true, std::memory_order_release);
  refreshRequested_.store(true, std::memory_order_release);
  Serial.println("[BOOT_PREFETCH] start");
}

bool LiveFeedsManager::isBootDeadlineExpired() const {
  if (!bootPrefetchActive_.load(std::memory_order_acquire)) return false;
  if (bootPrefetchAbortRequested_.load(std::memory_order_acquire)) return true;
  uint32_t start = bootPrefetchStartMs_.load(std::memory_order_acquire);
  if (start == 0) return false;
  return (millis() - start >= kBootPrefetchTimeoutMs);
}

// MUST only be executed on Core 0 worker thread!
void LiveFeedsManager::abortBootPrefetchAndPublish(const char* reason) {
  Serial.printf("[BOOT_PREFETCH] Aborted (%s). Publishing partial snapshot on Core 0.\n", reason);
  localSnapshot_.valid = (localSnapshot_.cachedNewsCount > 0 || localSnapshot_.weather.valid);

  // Never write publishedSnapshot_ without possessing snapshotMutex_!
  bool locked = false;
  for (int retry = 0; retry < 10; ++retry) {
    if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
      locked = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (locked) {
    publishedSnapshot_ = localSnapshot_;
    xSemaphoreGive(snapshotMutex_);
    Serial.println("[LiveFeedsWorker] Published partial snapshot under mutex successfully.");
  } else {
    Serial.println("[LiveFeedsWorker] ERROR: Could not acquire snapshotMutex_ for partial snapshot. Skipping write.");
  }

  Serial.println("[BOOT_PREFETCH] snapshot published");
  uint32_t start = bootPrefetchStartMs_.load(std::memory_order_acquire);
  uint32_t totalElapsed = (start > 0) ? (millis() - start) : 0;
  Serial.printf("[BOOT_PREFETCH] total elapsed=%u ms\n", static_cast<unsigned>(totalElapsed));

  bootPrefetchAborted_.store(true, std::memory_order_release);
  bootPrefetchActive_.store(false, std::memory_order_release);
  bootPrefetchCompleted_.store(true, std::memory_order_release);

  step_.store(CycleStep::Idle, std::memory_order_release);
  state_.store(WorkerState::Idle, std::memory_order_release);
  lastRefreshCycleMs_ = millis();
}

// Executed on Core 1 (setup/main): ONLY signals abort request and waits for Core 0 worker to finish
bool LiveFeedsManager::waitForBootPrefetch(uint32_t timeoutMs) {
  uint32_t start = bootPrefetchStartMs_.load(std::memory_order_acquire);
  if (start == 0) start = millis();

  while (true) {
    bool done = (bootPrefetchCompleted_.load(std::memory_order_acquire) ||
                 bootPrefetchAborted_.load(std::memory_order_acquire));
    bool inFlight = requestInFlight_.load(std::memory_order_acquire);

    // Can only release Gemini Live when NO request is in flight AND either completed or aborted
    if (done && !inFlight) {
      return !bootPrefetchAborted_.load(std::memory_order_acquire);
    }

    uint32_t now = millis();
    if (now - start >= timeoutMs) {
      // Core 1 signals abort request; DOES NOT touch worker internal state or mutex!
      if (!bootPrefetchAbortRequested_.load(std::memory_order_acquire)) {
        Serial.printf("[BOOT_PREFETCH] Core 1 deadline reached (%u ms). Requesting worker abort.\n",
                      static_cast<unsigned>(timeoutMs));
        bootPrefetchAbortRequested_.store(true, std::memory_order_release);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void LiveFeedsManager::loop() {
  // Purely diagnostic / lightweight HWM monitor (<1 µs). Zero HTTP/network work.
  static uint32_t lastDiagMs = 0;
  uint32_t now = millis();
  if (now - lastDiagMs >= 60000UL) {
    lastDiagMs = now;
    if (workerTaskHandle_) {
      UBaseType_t hwm = uxTaskGetStackHighWaterMark(workerTaskHandle_);
      Serial.printf("[LiveFeeds] Background worker HWM: %u bytes remaining\n", static_cast<unsigned>(hwm));
    }
  }
}

void LiveFeedsManager::requestRefresh() {
  refreshRequested_.store(true, std::memory_order_release);
  Serial.println("[LiveFeeds] Refresh requested (enqueued for worker).");
}

void LiveFeedsManager::forceRefresh() {
  requestRefresh();
}

void LiveFeedsManager::requestCalendarRefresh() {
  calendarRefreshRequested_.store(true, std::memory_order_release);
  if (calendar_) calendar_->requestRefresh();
  Serial.println("[LiveFeeds] Calendar refresh requested (enqueued for worker).");
}

uint32_t LiveFeedsManager::getStackHighWaterMark() const {
  if (!workerTaskHandle_) return 0;
  return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(workerTaskHandle_));
}

LiveFeedsManager::LiveFeedsSnapshot LiveFeedsManager::getSnapshot() const {
  LiveFeedsSnapshot copy;
  if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    copy = publishedSnapshot_;
    xSemaphoreGive(snapshotMutex_);
  } else {
    copy = publishedSnapshot_;
  }
  return copy;
}

LiveFeedsManager::WeatherData LiveFeedsManager::weather() const {
  WeatherData copy;
  if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    copy = publishedSnapshot_.weather;
    xSemaphoreGive(snapshotMutex_);
  } else {
    copy = publishedSnapshot_.weather;
  }
  return copy;
}

String LiveFeedsManager::usdBrl() const {
  char buf[16] = {0};
  if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    strncpy(buf, publishedSnapshot_.usdBrl, sizeof(buf) - 1);
    xSemaphoreGive(snapshotMutex_);
  } else {
    strncpy(buf, publishedSnapshot_.usdBrl, sizeof(buf) - 1);
  }
  return String(buf);
}

String LiveFeedsManager::latestNews() const {
  char buf[128] = {0};
  if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    strncpy(buf, publishedSnapshot_.currentNewsHeadline, sizeof(buf) - 1);
    xSemaphoreGive(snapshotMutex_);
  } else {
    strncpy(buf, publishedSnapshot_.currentNewsHeadline, sizeof(buf) - 1);
  }
  return String(buf);
}

uint8_t LiveFeedsManager::cachedNewsCount() const {
  uint8_t count = 0;
  if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    count = publishedSnapshot_.cachedNewsCount;
    xSemaphoreGive(snapshotMutex_);
  } else {
    count = publishedSnapshot_.cachedNewsCount;
  }
  return count;
}

LiveFeedsManager::DreamCardData LiveFeedsManager::getDreamCard(uint8_t index) const {
  static const DreamCardData kStaticDreams[] = {
    {"Natureza", "Plantar horta", "Cuidar das mudas de manjericao"},
    {"Financas", "Declaracao de IR", "Recolher informes e despesas"},
    {"Familia", "Festa do filhinho", "Contratar buffet e convidados"},
    {"Viagens", "Ferias em familia", "Passagens, hotel e roteiro"},
    {"Carreira", "Seguranca emprego", "Avaliacao e metas pendentes"},
    {"Vida", "Revisao do carro", "Agendar manutencao anual"},
    {"Educacao", "Faculdade criancas", "Aportes e planos longo prazo"},
    {"Hobby", "Ler livro novo", "Terminar o capitulo da semana"},
    {"Saude", "Treino de corrida", "Preparacao para correr 5km"},
    {"Arte", "Aprender aquarela", "Mistura de cores e luz"},
    {"Culinaria", "Testar receita", "Fazer bolo de chocolate fofo"},
    {"Bem-estar", "Meditacao zen", "15 minutos de calma e silencio"},
    {"Musica", "Tocar teclado", "Aprender uma musica nova"},
  };
  static constexpr size_t kStaticCount = sizeof(kStaticDreams) / sizeof(kStaticDreams[0]);

  uint8_t totalSlots = kStaticCount + 2; // +1 weather, +1 currency/news
  uint8_t slot = index % totalSlots;

  if (slot == kStaticCount) {
    static char s_weatherTitle[48];
    static char s_weatherDetail[64];
    WeatherData w;
    if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
      w = publishedSnapshot_.weather;
      xSemaphoreGive(snapshotMutex_);
    } else {
      w = publishedSnapshot_.weather;
    }
    snprintf(s_weatherTitle, sizeof(s_weatherTitle), "Brasilia: %.1f C", w.temp);
    snprintf(s_weatherDetail, sizeof(s_weatherDetail), "Umidade: %.0f%% | Vento: %.0f km/h", w.humidity, w.wind);
    return {"Clima", s_weatherTitle, s_weatherDetail};
  } else if (slot == kStaticCount + 1) {
    static char s_curTitle[48];
    static char s_headline[128];
    if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
      snprintf(s_curTitle, sizeof(s_curTitle), "Cotacao USD: R$ %s", publishedSnapshot_.usdBrl);
      strncpy(s_headline, publishedSnapshot_.currentNewsHeadline, sizeof(s_headline) - 1);
      s_headline[sizeof(s_headline) - 1] = '\0';
      xSemaphoreGive(snapshotMutex_);
    } else {
      snprintf(s_curTitle, sizeof(s_curTitle), "Cotacao USD: R$ %s", publishedSnapshot_.usdBrl);
      strncpy(s_headline, publishedSnapshot_.currentNewsHeadline, sizeof(s_headline) - 1);
      s_headline[sizeof(s_headline) - 1] = '\0';
    }
    return {"Economia", s_curTitle, s_headline};
  }

  return kStaticDreams[slot];
}

bool LiveFeedsManager::canStartRequest() const {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (TlsArbiter::instance().isGeminiActive()) return false;
  if (gemini_ && gemini_->isConnectingOrHandshaking()) return false;
  if (bootPrefetchActive_.load(std::memory_order_acquire)) return true;
  // Core 1 loopTask evaluates system idle state (IDLE or Sleep; blocks on LISTENING, WAITING_MODEL, TOOL_CALL, SPEAKING)
  return systemIdle_.load(std::memory_order_acquire);
}

bool LiveFeedsManager::canStartCalendarFetch() const {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!systemIdle_.load(std::memory_order_acquire)) return false;
  if (TlsArbiter::instance().isGeminiActive()) return false;

  if (gemini_) {
    if (gemini_->isSpeaking() || gemini_->isRecording() || gemini_->isTurnBusy()) return false;
    if (gemini_->turnState() != GeminiLiveProbe::LiveTurnState::IDLE) return false;
    if (gemini_->isConnectingOrHandshaking()) return false;
  }

  if (!calendarJobInFlight_.load(std::memory_order_acquire) && requestInFlight_.load(std::memory_order_acquire)) return false;
  if (step_.load(std::memory_order_acquire) != CycleStep::Idle) return false;

  time_t nowSec = time(nullptr);
  struct tm nowTm {};
  localtime_r(&nowSec, &nowTm);
  if ((nowTm.tm_year + 1900) < 2024) return false;

  size_t freeHeap = ESP.getFreeHeap();
  size_t maxAlloc = ESP.getMaxAllocHeap();
  if (freeHeap < 48000 || maxAlloc < 20000) return false;

  return true;
}

bool LiveFeedsManager::canContinueCalendarFetch() const {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!systemIdle_.load(std::memory_order_acquire)) return false;
  if (TlsArbiter::instance().isGeminiActive()) return false;

  if (gemini_) {
    if (gemini_->isSpeaking() || gemini_->isRecording() || gemini_->isTurnBusy()) return false;
    if (gemini_->turnState() != GeminiLiveProbe::LiveTurnState::IDLE) return false;
    if (gemini_->isConnectingOrHandshaking()) return false;
  }

  return true;
}

void LiveFeedsManager::executeCalendarFetch() {
  if (!calendar_) return;

  TlsFeedsGuard tlsGuard;
  if (!tlsGuard.isAcquired()) {
    calendarNextRetryMs_ = millis() + kCalendarFailureBackoffMs;
    return;
  }

  RequestInFlightGuard guard(requestInFlight_);
  calendarJobInFlight_.store(true, std::memory_order_release);
  state_.store(WorkerState::Running, std::memory_order_release);

  Serial.println("[LiveFeedsWorker] Starting background Google Calendar fetch on Core 0...");
  uint32_t t0 = millis();

  bool ok = calendar_->executeFetch([this]() -> bool {
    return canContinueCalendarFetch();
  });

  uint32_t elapsed = millis() - t0;
  lastCalendarAttemptMs_ = millis();

  if (ok) {
    lastCalendarFetchMs_ = millis();
    calendarNextRetryMs_ = 0;
    calendarRefreshRequested_.store(false, std::memory_order_release);
    Serial.printf("[LiveFeedsWorker] Google Calendar fetch complete (%u ms). Cache updated.\n",
                  static_cast<unsigned>(elapsed));
  } else {
    calendarNextRetryMs_ = millis() + kCalendarFailureBackoffMs;
    Serial.printf("[LiveFeedsWorker] Google Calendar fetch failed/aborted (%u ms). Backing off for %u s\n",
                  static_cast<unsigned>(elapsed),
                  static_cast<unsigned>(kCalendarFailureBackoffMs / 1000));
  }

  calendarJobInFlight_.store(false, std::memory_order_release);
  state_.store(WorkerState::Idle, std::memory_order_release);

  UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
  Serial.printf("[LiveFeedsWorker] Post-calendar Stack HWM: %u bytes remaining\n", static_cast<unsigned>(hwm));
}

void LiveFeedsManager::workerTaskThunk(void* param) {
  auto* self = static_cast<LiveFeedsManager*>(param);
  self->workerTaskLoop();
}

void LiveFeedsManager::workerTaskLoop() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (WiFi.status() != WL_CONNECTED) {
      continue;
    }

    uint32_t now = millis();

    // Check if a new refresh cycle should be triggered
    CycleStep currentStep = step_.load(std::memory_order_acquire);
    if (currentStep == CycleStep::Idle) {
      bool shouldTrigger = false;
      if (bootPrefetchActive_.load(std::memory_order_acquire)) {
        shouldTrigger = true;
      } else if (refreshRequested_.load(std::memory_order_acquire) || (now - lastRefreshCycleMs_ >= kRefreshIntervalMs)) {
        shouldTrigger = true;
      }

      if (shouldTrigger) {
        if (!canStartRequest()) {
          if (state_.load(std::memory_order_acquire) != WorkerState::WaitingIdle) {
            state_.store(WorkerState::WaitingIdle, std::memory_order_release);
            Serial.println("[LiveFeedsWorker] Triggered but system not idle. Entering WaitingIdle...");
          }
          continue;
        }

        refreshRequested_.store(false, std::memory_order_release);

        // Start cycle: begin with local copy of currently published snapshot to preserve valid fields
        if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
          localSnapshot_ = publishedSnapshot_;
          xSemaphoreGive(snapshotMutex_);
        }
        state_.store(WorkerState::Running, std::memory_order_release);
        step_.store(CycleStep::PendingNews, std::memory_order_release);
        currentStep = CycleStep::PendingNews;
        Serial.println("[LiveFeedsWorker] Starting sequential refresh cycle (News -> Weather -> USD)...");
      } else if (!bootPrefetchActive_.load(std::memory_order_acquire) && calendar_ && !calendarJobInFlight_.load(std::memory_order_acquire)) {
        bool calNeeded = false;
        if (calendar_->isRefreshRequested() || calendarRefreshRequested_.load(std::memory_order_acquire)) {
          calNeeded = true;
        } else if (!calendar_->hasCache() || !calendar_->isCacheValidForToday()) {
          if (now >= calendarNextRetryMs_ && (now - lastCalendarAttemptMs_ >= 60000UL)) {
            calNeeded = true;
          }
        } else if (now - lastCalendarFetchMs_ >= kCalendarRefreshIntervalMs) {
          calNeeded = true;
        }

        if (calNeeded && now >= calendarNextRetryMs_) {
          if (canStartCalendarFetch()) {
            executeCalendarFetch();
          }
        }
      }
    }

    // Active cycle state machine (pausable and resumable)
    if (currentStep != CycleStep::Idle) {
      // Step 1: News RSS (FIRST)
      if (currentStep == CycleStep::PendingNews) {
        if (bootPrefetchActive_.load(std::memory_order_acquire) && isBootDeadlineExpired()) {
          abortBootPrefetchAndPublish("deadline expired before News");
          continue;
        }
        if (!canStartRequest()) {
          if (state_.load(std::memory_order_acquire) != WorkerState::WaitingIdle) {
            state_.store(WorkerState::WaitingIdle, std::memory_order_release);
            Serial.println("[LiveFeedsWorker] System active before News. Pausing in WaitingIdle...");
          }
          continue;
        }
        state_.store(WorkerState::Running, std::memory_order_release);
        bool isBoot = bootPrefetchActive_.load(std::memory_order_acquire);
        uint32_t t0 = millis();
        if (isBoot) {
          Serial.println("[BOOT_PREFETCH] news start");
        }
        Serial.println("[LiveFeedsWorker] Step 1/3: Fetching RSS News...");
        fetchNews(localSnapshot_.cachedNews, localSnapshot_.cachedNewsCount, localSnapshot_.currentNewsHeadline);
        localSnapshot_.lastNewsUpdatedMs = millis();
        if (isBoot) {
          uint32_t elapsed = millis() - t0;
          Serial.printf("[BOOT_PREFETCH] news end status=%s items=%u elapsed=%u ms\n",
                        (localSnapshot_.cachedNewsCount > 0) ? "OK" : "FAILED",
                        localSnapshot_.cachedNewsCount,
                        static_cast<unsigned>(elapsed));
        }

        if (bootPrefetchActive_.load(std::memory_order_acquire) && isBootDeadlineExpired()) {
          abortBootPrefetchAndPublish("deadline expired after News");
          continue;
        }

        step_.store(CycleStep::PendingWeather, std::memory_order_release);
        currentStep = CycleStep::PendingWeather;
        vTaskDelay(pdMS_TO_TICKS(50));
      }

      // Step 2: Weather
      if (currentStep == CycleStep::PendingWeather) {
        if (bootPrefetchActive_.load(std::memory_order_acquire) && isBootDeadlineExpired()) {
          abortBootPrefetchAndPublish("deadline expired before Weather");
          continue;
        }
        if (!canStartRequest()) {
          if (state_.load(std::memory_order_acquire) != WorkerState::WaitingIdle) {
            state_.store(WorkerState::WaitingIdle, std::memory_order_release);
            Serial.println("[LiveFeedsWorker] System active before Weather. Pausing in WaitingIdle (News preserved)...");
          }
          continue;
        }
        state_.store(WorkerState::Running, std::memory_order_release);
        bool isBoot = bootPrefetchActive_.load(std::memory_order_acquire);
        uint32_t t0 = millis();
        if (isBoot) {
          Serial.println("[BOOT_PREFETCH] weather start");
        }
        Serial.println("[LiveFeedsWorker] Step 2/3: Fetching Weather...");
        fetchWeather(localSnapshot_.weather);
        localSnapshot_.lastWeatherUpdatedMs = millis();
        if (isBoot) {
          uint32_t elapsed = millis() - t0;
          Serial.printf("[BOOT_PREFETCH] weather end status=%s elapsed=%u ms\n",
                        localSnapshot_.weather.valid ? "OK" : "FAILED",
                        static_cast<unsigned>(elapsed));
        }

        if (bootPrefetchActive_.load(std::memory_order_acquire) && isBootDeadlineExpired()) {
          abortBootPrefetchAndPublish("deadline expired after Weather");
          continue;
        }

        step_.store(CycleStep::PendingUsd, std::memory_order_release);
        currentStep = CycleStep::PendingUsd;
        vTaskDelay(pdMS_TO_TICKS(50));
      }

      // Step 3: Currency (USD/BRL)
      if (currentStep == CycleStep::PendingUsd) {
        if (bootPrefetchActive_.load(std::memory_order_acquire) && isBootDeadlineExpired()) {
          abortBootPrefetchAndPublish("deadline expired before USD");
          continue;
        }
        if (!canStartRequest()) {
          if (state_.load(std::memory_order_acquire) != WorkerState::WaitingIdle) {
            state_.store(WorkerState::WaitingIdle, std::memory_order_release);
            Serial.println("[LiveFeedsWorker] System active before USD. Pausing in WaitingIdle (News & Weather preserved)...");
          }
          continue;
        }
        state_.store(WorkerState::Running, std::memory_order_release);
        bool isBoot = bootPrefetchActive_.load(std::memory_order_acquire);
        uint32_t t0 = millis();
        if (isBoot) {
          Serial.println("[BOOT_PREFETCH] usd start");
        }
        Serial.println("[LiveFeedsWorker] Step 3/3: Fetching USD/BRL currency...");
        fetchCurrency(localSnapshot_.usdBrl);
        localSnapshot_.lastUsdUpdatedMs = millis();
        if (isBoot) {
          uint32_t elapsed = millis() - t0;
          Serial.printf("[BOOT_PREFETCH] usd end status=%s elapsed=%u ms\n",
                        (localSnapshot_.usdBrl[0] != '\0' && strcmp(localSnapshot_.usdBrl, "5.18") != 0) ? "OK" : "DEFAULT",
                        static_cast<unsigned>(elapsed));
        }

        if (isBootDeadlineExpired()) {
          abortBootPrefetchAndPublish("deadline expired after USD");
          continue;
        }

        step_.store(CycleStep::Publishing, std::memory_order_release);
        currentStep = CycleStep::Publishing;
      }

      // Step 4: Publish snapshot
      if (currentStep == CycleStep::Publishing) {
        bool isBoot = bootPrefetchActive_.load(std::memory_order_acquire);
        // Valid if at least one service succeeded (news parsed items or weather valid)
        localSnapshot_.valid = (localSnapshot_.cachedNewsCount > 0 || localSnapshot_.weather.valid);

        // Never write publishedSnapshot_ without possessing snapshotMutex_!
        bool locked = false;
        for (int retry = 0; retry < 10; ++retry) {
          if (snapshotMutex_ && xSemaphoreTake(snapshotMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
            locked = true;
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (locked) {
          publishedSnapshot_ = localSnapshot_;
          xSemaphoreGive(snapshotMutex_);
          Serial.println("[LiveFeedsWorker] Published new snapshot under mutex successfully.");
        } else {
          Serial.println("[LiveFeedsWorker] ERROR: Could not acquire snapshotMutex_ on publish. Skipping write.");
        }

        if (isBoot) {
          Serial.println("[BOOT_PREFETCH] snapshot published");
          uint32_t start = bootPrefetchStartMs_.load(std::memory_order_acquire);
          uint32_t totalElapsed = (start > 0) ? (millis() - start) : 0;
          Serial.printf("[BOOT_PREFETCH] total elapsed=%u ms\n", static_cast<unsigned>(totalElapsed));
          bootPrefetchActive_.store(false, std::memory_order_release);
          bootPrefetchCompleted_.store(true, std::memory_order_release);
        }

        step_.store(CycleStep::Idle, std::memory_order_release);
        state_.store(WorkerState::Idle, std::memory_order_release);
        lastRefreshCycleMs_ = millis();

        UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
        Serial.printf("[LiveFeedsWorker] Cycle complete. Stack HWM: %u bytes remaining (stack: 16384 bytes)\n",
                      static_cast<unsigned>(hwm));
      }
    }
  }
}

void LiveFeedsManager::fetchWeather(WeatherData& outWeather) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (TlsArbiter::instance().isGeminiActive()) return;
  RequestInFlightGuard guard(requestInFlight_);
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  if (http.begin("http://api.open-meteo.com/v1/forecast?latitude=-15.7801&longitude=-47.9292&current=temperature_2m,relative_humidity_2m,wind_speed_10m&timezone=America%2FSao_Paulo")) {
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      String payload = http.getString();
      JsonDocument doc;
      if (!deserializeJson(doc, payload)) {
        outWeather.temp = doc["current"]["temperature_2m"] | outWeather.temp;
        outWeather.humidity = doc["current"]["relative_humidity_2m"] | outWeather.humidity;
        outWeather.wind = doc["current"]["wind_speed_10m"] | outWeather.wind;
        outWeather.valid = true;
        Serial.printf("[LiveFeedsWorker] Weather Brasilia: temp=%.1fC hum=%.1f%%\n", outWeather.temp, outWeather.humidity);
      }
    } else {
      Serial.printf("[LiveFeedsWorker] Weather HTTP error: %d\n", code);
    }
    http.end();
  }
}

void LiveFeedsManager::fetchCurrency(char* outUsdBrl) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (TlsArbiter::instance().isGeminiActive()) return;
  RequestInFlightGuard guard(requestInFlight_);
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  if (http.begin("http://open.er-api.com/v6/latest/USD")) {
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      String payload = http.getString();
      JsonDocument doc;
      if (!deserializeJson(doc, payload)) {
        float rate = doc["rates"]["BRL"] | 0.0f;
        if (rate > 1.0f) {
          snprintf(outUsdBrl, 8, "%.2f", rate);
          Serial.printf("[LiveFeedsWorker] USD/BRL = %s\n", outUsdBrl);
        }
      }
    } else {
      Serial.printf("[LiveFeedsWorker] Currency HTTP error: %d\n", code);
    }
    http.end();
  }
}

namespace {
class StatefulXmlStreamParser : public Stream {
 public:
  StatefulXmlStreamParser(WiFiClientSecure* client, LiveFeedsManager::NewsItem* outNews,
                          uint8_t& outCount, uint8_t maxTotal, const char* category,
                          char* outHeadline)
      : client_(client), outNews_(outNews), count_(outCount), maxTotal_(maxTotal),
        category_(category), outHeadline_(outHeadline) {}

  size_t write(uint8_t b) override {
    uint8_t buf[1] = {b};
    return write(buf, 1);
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!buffer || size == 0 || done_) return size;

    size_t offset = 0;
    while (offset < size && !done_) {
      size_t space = (kAccumCap - 1) - accumLen_;
      if (space == 0) {
        processAccumulator();
        if (accumLen_ >= (kAccumCap - 1)) {
          size_t keep = 512;
          memmove(accum_, accum_ + (accumLen_ - keep), keep);
          accumLen_ = keep;
          accum_[accumLen_] = '\0';
        }
        space = (kAccumCap - 1) - accumLen_;
      }

      size_t toCopy = size - offset;
      if (toCopy > space) toCopy = space;

      memcpy(accum_ + accumLen_, buffer + offset, toCopy);
      accumLen_ += toCopy;
      accum_[accumLen_] = '\0';
      offset += toCopy;

      processAccumulator();
    }
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  bool isDone() const { return done_; }

 private:
  static constexpr size_t kAccumCap = 3072;
  char accum_[kAccumCap];
  size_t accumLen_ = 0;

  WiFiClientSecure* client_ = nullptr;
  LiveFeedsManager::NewsItem* outNews_ = nullptr;
  uint8_t& count_;
  uint8_t maxTotal_ = 0;
  const char* category_ = nullptr;
  char* outHeadline_ = nullptr;
  bool inItem_ = false;
  bool done_ = false;

  void processAccumulator() {
    while (!done_) {
      if (!inItem_) {
        const char* p = strstr(accum_, "<item");
        if (p) {
          size_t pos = p - accum_;
          if (pos > 0) {
            memmove(accum_, p, accumLen_ - pos);
            accumLen_ -= pos;
            accum_[accumLen_] = '\0';
          }
          inItem_ = true;
        } else {
          if (accumLen_ > 5) {
            memmove(accum_, accum_ + (accumLen_ - 5), 5);
            accumLen_ = 5;
            accum_[accumLen_] = '\0';
          }
          break;
        }
      }

      if (inItem_) {
        const char* end = strstr(accum_, "</item>");
        if (end) {
          size_t itemLen = (end + 7) - accum_;
          parseAndStoreItem(accum_, itemLen);

          size_t remaining = accumLen_ - itemLen;
          memmove(accum_, accum_ + itemLen, remaining);
          accumLen_ = remaining;
          accum_[accumLen_] = '\0';
          inItem_ = false;

          if (count_ >= maxTotal_) {
            done_ = true;
            if (client_) client_->stop();
            break;
          }
        } else {
          break;
        }
      }
    }
  }

  void parseAndStoreItem(const char* itemXml, size_t itemLen) {
    if (count_ >= maxTotal_) return;

    char title[128] = {0};
    char link[128] = {0};
    char desc[224] = {0};
    char source[64] = {0};

    extractTag(itemXml, "title", title, sizeof(title));
    extractTag(itemXml, "link", link, sizeof(link));
    extractTag(itemXml, "description", desc, sizeof(desc));
    extractTag(itemXml, "source", source, sizeof(source));

    if (title[0] != '\0') {
      strncpy(outNews_[count_].category, category_, sizeof(outNews_[count_].category) - 1);
      outNews_[count_].category[sizeof(outNews_[count_].category) - 1] = '\0';

      strncpy(outNews_[count_].title, title, sizeof(outNews_[count_].title) - 1);
      outNews_[count_].title[sizeof(outNews_[count_].title) - 1] = '\0';

      if (desc[0] != '\0') {
        strncpy(outNews_[count_].summary, desc, sizeof(outNews_[count_].summary) - 1);
      } else if (source[0] != '\0') {
        snprintf(outNews_[count_].summary, sizeof(outNews_[count_].summary), "Fonte: %s", source);
      } else {
        outNews_[count_].summary[0] = '\0';
      }
      outNews_[count_].summary[sizeof(outNews_[count_].summary) - 1] = '\0';

      strncpy(outNews_[count_].link, link, sizeof(outNews_[count_].link) - 1);
      outNews_[count_].link[sizeof(outNews_[count_].link) - 1] = '\0';

      if (count_ == 0 && outHeadline_ && outHeadline_[0] == '\0') {
        strncpy(outHeadline_, title, 127);
        outHeadline_[127] = '\0';
      }
      count_++;
    }
  }

  static bool extractTag(const char* xml, const char* tag, char* out, size_t maxOutLen) {
    if (!xml || !tag || !out || maxOutLen == 0) return false;
    out[0] = '\0';

    char openTag[32];
    snprintf(openTag, sizeof(openTag), "<%s", tag);
    const char* p = strstr(xml, openTag);
    if (!p) return false;

    p = strchr(p, '>');
    if (!p) return false;
    p++;

    char closeTag[32];
    snprintf(closeTag, sizeof(closeTag), "</%s>", tag);
    const char* end = strstr(p, closeTag);
    if (!end) return false;

    size_t valLen = end - p;
    const char* cdataStart = strstr(p, "<![CDATA[");
    if (cdataStart && cdataStart < end) {
      p = cdataStart + 9;
      const char* cdataEnd = strstr(p, "]]>");
      if (cdataEnd && cdataEnd < end) {
        valLen = cdataEnd - p;
      }
    }

    size_t outIdx = 0;
    bool inHtml = false;
    bool lastSpace = false;

    while (valLen > 0 && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
      p++;
      valLen--;
    }

    for (size_t i = 0; i < valLen && outIdx < maxOutLen - 1; ++i) {
      char c = p[i];
      if (c == '<') { inHtml = true; continue; }
      if (inHtml) {
        if (c == '>') inHtml = false;
        continue;
      }
      if (c == '&') {
        if (strncmp(&p[i], "&amp;", 5) == 0) { c = '&'; i += 4; }
        else if (strncmp(&p[i], "&quot;", 6) == 0) { c = '"'; i += 5; }
        else if (strncmp(&p[i], "&#39;", 5) == 0 || strncmp(&p[i], "&apos;", 6) == 0) {
          c = '\'';
          i += (p[i + 1] == '#') ? 4 : 5;
        }
        else if (strncmp(&p[i], "&lt;", 4) == 0) { c = '<'; i += 3; }
        else if (strncmp(&p[i], "&gt;", 4) == 0) { c = '>'; i += 3; }
        else if (strncmp(&p[i], "&nbsp;", 6) == 0) { c = ' '; i += 5; }
      }

      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        if (!lastSpace && outIdx > 0) {
          out[outIdx++] = ' ';
          lastSpace = true;
        }
        continue;
      }
      lastSpace = false;
      out[outIdx++] = c;
    }
    while (outIdx > 0 && out[outIdx - 1] == ' ') {
      outIdx--;
    }
    out[outIdx] = '\0';
    return outIdx > 0;
  }
};
}  // namespace

void LiveFeedsManager::fetchNews(NewsItem* outNews, uint8_t& outCount, char* outHeadline) {
  if (WiFi.status() != WL_CONNECTED) return;

  TlsFeedsGuard tlsGuard;
  if (!tlsGuard.isAcquired()) {
    return;
  }

  struct FeedDef {
    const char* url;
    const char* category;
    uint8_t targetCount;
  };
  const FeedDef kFeeds[] = {
    {"https://news.google.com/rss/headlines/section/topic/POLITICS?hl=pt-BR&gl=BR&ceid=BR:pt-419", "politica", 4},
    {"https://news.google.com/rss?hl=pt-BR&gl=BR&ceid=BR:pt-419", "geral", 8}
  };

  uint8_t count = 0;
  const char* kCollectHeaders[] = {"Content-Encoding", "Transfer-Encoding"};

  for (const auto& feed : kFeeds) {
    if (count >= kMaxCachedNews) break;
    if (isBootDeadlineExpired()) {
      Serial.println("[BOOT_PREFETCH] Deadline expired; breaking out of news feeds loop.");
      break;
    }
    if (TlsArbiter::instance().isGeminiPriorityPending() || !canStartRequest()) {
      Serial.println("[LiveFeedsWorker] News feed loop paused: Gemini active, avoiding concurrent TLS handshake");
      break;
    }

    RequestInFlightGuard guard(requestInFlight_);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(kHttpTimeoutMs);

    if (!http.begin(client, feed.url)) {
      Serial.printf("[LiveFeedsWorker] News begin failed for %s\n", feed.url);
      continue;
    }

    http.collectHeaders(kCollectHeaders, 2);
    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) StackChan/1.0");
    http.addHeader("Accept-Encoding", "identity");

    int code = http.GET();
    if (code == HTTP_CODE_OK || code == HTTP_CODE_MOVED_PERMANENTLY) {
      String encoding = http.header("Content-Encoding");
      String transfer = http.header("Transfer-Encoding");
      if (encoding.indexOf("gzip") >= 0) {
        Serial.println("[LiveFeedsWorker] GZIP encoding detected from server");
      }

      StatefulXmlStreamParser parser(&client, outNews, count, feed.targetCount, feed.category, outHeadline);
      int bytesWritten = http.writeToStream(&parser);
      Serial.printf("[LiveFeedsWorker] Streamed %s: %d bytes parsed (transfer=%s encoding=%s items_total=%u)\n",
                    feed.category, bytesWritten, transfer.c_str(), encoding.c_str(), count);
    } else {
      Serial.printf("[LiveFeedsWorker] News HTTP error %d from %s\n", code, feed.url);
    }
    http.end();
    client.stop();
  }

  if (count > 0) {
    outCount = count;
    Serial.printf("[LiveFeedsWorker] Refreshed %u RSS news items\n", count);
  } else {
    Serial.println("[LiveFeedsWorker] News fetch produced 0 items; preserving stale cache if valid");
  }
}

