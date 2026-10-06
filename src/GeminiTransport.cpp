#include "GeminiTransport.h"
#include "TlsArbiter.h"
#include <esp_heap_caps.h>

GeminiTransport* GeminiTransport::instance_ = nullptr;

GeminiTransport::GeminiTransport() {
  instance_ = this;
}

GeminiTransport::~GeminiTransport() {
  stopTask();
  TlsArbiter::instance().releaseGemini();
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

bool GeminiTransport::begin(const String& apiKey) {
  api_key_ = apiKey;

  // Allocate Inbound Ring (512 KB) in PSRAM
  if (!inbound_ring_) {
    inbound_storage_ = static_cast<uint8_t*>(ps_malloc(INBOUND_BUFFER_BYTES));
    if (!inbound_storage_) {
      Serial.printf("[ERROR] GeminiTransport: ps_malloc failed for inbound ring (%u bytes)\n",
                    static_cast<unsigned>(INBOUND_BUFFER_BYTES));
      return false;
    }
    inbound_struct_ = static_cast<StaticRingbuffer_t*>(malloc(sizeof(StaticRingbuffer_t)));
    if (!inbound_struct_) {
      free(inbound_storage_);
      inbound_storage_ = nullptr;
      return false;
    }
    inbound_ring_ = xRingbufferCreateStatic(INBOUND_BUFFER_BYTES, RINGBUF_TYPE_NOSPLIT,
                                           inbound_storage_, inbound_struct_);
    if (!inbound_ring_) {
      Serial.println("[ERROR] GeminiTransport: xRingbufferCreateStatic failed for inbound ring");
      return false;
    }
    inbound_max_item_size_ = xRingbufferGetMaxItemSize(inbound_ring_);
  }

  // Allocate Outbound Ring (512 KB) in PSRAM
  if (!outbound_ring_) {
    outbound_storage_ = static_cast<uint8_t*>(ps_malloc(OUTBOUND_BUFFER_BYTES));
    if (!outbound_storage_) {
      Serial.printf("[ERROR] GeminiTransport: ps_malloc failed for outbound ring (%u bytes)\n",
                    static_cast<unsigned>(OUTBOUND_BUFFER_BYTES));
      return false;
    }
    outbound_struct_ = static_cast<StaticRingbuffer_t*>(malloc(sizeof(StaticRingbuffer_t)));
    if (!outbound_struct_) {
      free(outbound_storage_);
      outbound_storage_ = nullptr;
      return false;
    }
    outbound_ring_ = xRingbufferCreateStatic(OUTBOUND_BUFFER_BYTES, RINGBUF_TYPE_NOSPLIT,
                                            outbound_storage_, outbound_struct_);
    if (!outbound_ring_) {
      Serial.println("[ERROR] GeminiTransport: xRingbufferCreateStatic failed for outbound ring");
      return false;
    }
    outbound_max_item_size_ = xRingbufferGetMaxItemSize(outbound_ring_);
  }

  // Command queue (Core 1 -> Core 0)
  if (!cmd_queue_) {
    cmd_queue_ = xQueueCreate(CMD_QUEUE_DEPTH, sizeof(TransportCmd));
    if (!cmd_queue_) {
      Serial.println("[ERROR] GeminiTransport: failed to create cmd_queue_");
      return false;
    }
  }

  Serial.printf("[NET:TRANSPORT] Inbound max=%u bytes, Outbound max=%u bytes\n",
                static_cast<unsigned>(inbound_max_item_size_),
                static_cast<unsigned>(outbound_max_item_size_));

  // Boot check: xRingbufferGetMaxItemSize() >= 153600 + sizeof(InboundEventHeader)
  const size_t min_required = 153600 + sizeof(InboundEventHeader);
  if (inbound_max_item_size_ < min_required) {
    Serial.printf("[FATAL] GeminiTransport: inbound_max_item_size_ %u < required %u\n",
                  static_cast<unsigned>(inbound_max_item_size_),
                  static_cast<unsigned>(min_required));
    return false;
  }

  return true;
}

bool GeminiTransport::startTask() {
  if (task_handle_) return true;

  running_ = true;
  BaseType_t rc = xTaskCreatePinnedToCore(
      taskEntry,
      "GeminiWsTask",
      TASK_STACK_SIZE,
      this,
      TASK_PRIORITY,
      &task_handle_,
      TASK_CORE);

  if (rc != pdPASS) {
    Serial.println("[ERROR] GeminiTransport: failed to create GeminiWsTask on Core 0");
    running_ = false;
    task_handle_ = nullptr;
    return false;
  }
  return true;
}

void GeminiTransport::stopTask() {
  if (!task_handle_) return;

  TransportCmd cmd{TransportCmdType::SHUTDOWN, current_epoch_};
  if (cmd_queue_) {
    xQueueSend(cmd_queue_, &cmd, 0);
  }

  uint32_t t0 = millis();
  while (running_ && millis() - t0 < 500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  task_handle_ = nullptr;
}

bool GeminiTransport::requestConnect(uint32_t epoch) {
  if (!cmd_queue_) return false;
  TlsArbiter::instance().requestGeminiPriority();
  TransportCmd cmd{TransportCmdType::CONNECT, epoch};
  return xQueueSend(cmd_queue_, &cmd, 0) == pdTRUE;
}

bool GeminiTransport::requestDisconnect(uint32_t epoch) {
  if (!cmd_queue_) return false;
  TransportCmd cmd{TransportCmdType::DISCONNECT, epoch};
  return xQueueSend(cmd_queue_, &cmd, 0) == pdTRUE;
}

bool GeminiTransport::enqueue(OutboundKind kind, const char* data, size_t len, uint32_t epoch) {
  if (!outbound_ring_ || !data || len == 0) return false;

  size_t total_size = sizeof(OutboundHeader) + len;
  if (total_size > outbound_max_item_size_) {
    Serial.printf("[NET:OUTBOUND_OVERFLOW] item size %u > max %u\n",
                  static_cast<unsigned>(total_size),
                  static_cast<unsigned>(outbound_max_item_size_));
    return false;
  }

  if (kind == OutboundKind::AUDIO) {
    if (!is_connected_) {
      mic_tx_dropped_++;
      return false;
    }
    // Limit audio so it doesn't take more than ~half the ring (256 KB)
    if (outbound_audio_bytes_queued_ >= (OUTBOUND_BUFFER_BYTES / 2)) {
      mic_tx_dropped_++;
      return false;
    }
  }

  void* item = nullptr;
  if (xRingbufferSendAcquire(outbound_ring_, &item, total_size, 0) == pdTRUE) {
    OutboundHeader* hdr = static_cast<OutboundHeader*>(item);
    hdr->kind = kind;
    hdr->epoch = epoch;
    hdr->length = len;
    memcpy(static_cast<uint8_t*>(item) + sizeof(OutboundHeader), data, len);
    xRingbufferSendComplete(outbound_ring_, item);

    if (kind == OutboundKind::AUDIO) {
      outbound_audio_bytes_queued_ += total_size;
    }

    size_t cur_free = xRingbufferGetCurFreeSize(outbound_ring_);
    size_t used = OUTBOUND_BUFFER_BYTES > cur_free ? (OUTBOUND_BUFFER_BYTES - cur_free) : 0;
    if (used > tx_hwm_) tx_hwm_ = used;
    return true;
  }

  if (kind == OutboundKind::AUDIO) {
    mic_tx_dropped_++;
  } else {
    ctrl_fail_++;
    Serial.printf("[NET:CTRL_QUEUE_FULL] kind=%u len=%u epoch=%lu\n",
                  static_cast<unsigned>(kind), static_cast<unsigned>(len),
                  static_cast<unsigned long>(epoch));
  }
  return false;
}

void* GeminiTransport::receiveInbound(size_t* item_size) {
  if (!inbound_ring_) return nullptr;
  return xRingbufferReceive(inbound_ring_, item_size, 0);
}

void GeminiTransport::returnInboundItem(void* item) {
  if (!inbound_ring_ || !item) return;
  vRingbufferReturnItem(inbound_ring_, item);
}

UBaseType_t GeminiTransport::getTaskStackHwm() const {
  if (!task_handle_) return 0;
  return uxTaskGetStackHighWaterMark(task_handle_);
}

void GeminiTransport::taskEntry(void* arg) {
  GeminiTransport* self = static_cast<GeminiTransport*>(arg);
  self->taskLoop();
  vTaskDelete(nullptr);
}

void GeminiTransport::startConnectAttempt() {
  if (api_key_.length() == 0) return;

  // TLS-GATE-1: Acquire exclusive TLS ownership for Gemini before beginSSL
  uint32_t wait_start = millis();
  while (!TlsArbiter::instance().acquireGemini()) {
    vTaskDelay(pdMS_TO_TICKS(10));
    if (millis() - wait_start >= 3500) {
      Serial.println("[TLS:GATE] Gemini wait timeout for LiveFeeds; forcing takeover");
      TlsArbiter::instance().forceTakeoverGemini();
      break;
    }
  }

  // NET-2.1: Before any beginSSL() that is a retry or re-init, clean up old client/socket
  if (has_initialized_) {
    uint32_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    in_tls_cleanup_ = true;
    ws_.disconnect();
    in_tls_cleanup_ = false;

    uint32_t free_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t largest_after = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    if (retry_count_ > 0) {
      Serial.printf("[NET:TLS_CLEANUP] epoch=%lu retry=%u free_before=%u largest_before=%u free_after=%u largest_after=%u\n",
                    static_cast<unsigned long>(current_epoch_),
                    static_cast<unsigned>(retry_count_),
                    static_cast<unsigned>(free_before),
                    static_cast<unsigned>(largest_before),
                    static_cast<unsigned>(free_after),
                    static_cast<unsigned>(largest_after));
    }
  }
  has_initialized_ = true;

  uint32_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t max_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  Serial.printf("[NET:ATTEMPT] epoch=%lu retry=%u internal_free=%u largest_block=%u\n",
                static_cast<unsigned long>(current_epoch_),
                static_cast<unsigned>(retry_count_),
                static_cast<unsigned>(int_free),
                static_cast<unsigned>(max_block));

  state_ = TransportState::CONNECTING;
  connection_was_open_ = false;
  is_connected_ = false;

  ws_.beginSSL("generativelanguage.googleapis.com", 443,
               String("/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=") + api_key_);
  ws_.onEvent(GeminiTransport::wsEventStatic);
  ws_.setReconnectInterval(5000);
  ws_.enableHeartbeat(15000, 4000, 2);
}

void GeminiTransport::taskLoop() {
  Serial.printf("[NET:TRANSPORT] task_core=%d stack_hwm=%u\n",
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

  uint32_t last_ring_diag_ms = millis();

  while (running_) {
    // 1. Drain command queue
    TransportCmd cmd;
    while (xQueueReceive(cmd_queue_, &cmd, 0) == pdTRUE) {
      handleCmd(cmd);
    }

    uint32_t now = millis();

    // 2. If in BACKOFF, check if timer expired
    if (desired_connected_ && state_ == TransportState::BACKOFF) {
      if (now >= next_retry_ms_) {
        startConnectAttempt();
      }
    }

    // 3. If CONNECTING or OPEN, run loop
    if (desired_connected_ && (state_ == TransportState::CONNECTING || state_ == TransportState::OPEN)) {
      if (state_ == TransportState::OPEN) {
        drainOutbound();
      }

      uint32_t t0 = millis();
      ws_.loop();
      uint32_t dur = millis() - t0;
      if (dur >= 100) {
        Serial.printf("[NET:WS_BLOCK] op=loop dur_ms=%u core=0 epoch=%lu\n",
                      static_cast<unsigned>(dur), static_cast<unsigned long>(current_epoch_));
      }

      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      // In BACKOFF or DOWN: yield CPU without calling ws_.loop()
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Periodic telemetry log every 30 seconds
    now = millis();
    if (now - last_ring_diag_ms >= 30000) {
      last_ring_diag_ms = now;
      Serial.printf("[NET:RING] rx_hwm=%u tx_hwm=%u audio_drop=%u ctrl_fail=%u state=%u\n",
                    static_cast<unsigned>(rx_hwm_), static_cast<unsigned>(tx_hwm_),
                    static_cast<unsigned>(mic_tx_dropped_), static_cast<unsigned>(ctrl_fail_),
                    static_cast<unsigned>(state_));
    }
  }
}

void GeminiTransport::handleCmd(const TransportCmd& cmd) {
  if (cmd.type == TransportCmdType::CONNECT) {
    // Rule 3: Idempotency check
    if (desired_connected_ && cmd.epoch == current_epoch_ && state_ != TransportState::DOWN) {
      Serial.printf("[NET:CONNECT_DUP] epoch=%lu ignored=1 state=%u\n",
                    static_cast<unsigned long>(cmd.epoch), static_cast<unsigned>(state_));
      return;
    }

    if (cmd.epoch < current_epoch_) {
      return;
    }

    current_epoch_ = cmd.epoch;
    desired_connected_ = true;
    retry_count_ = 0;
    next_retry_ms_ = 0;
    Serial.printf("[NET:CONNECT] epoch=%lu\n", static_cast<unsigned long>(current_epoch_));

    startConnectAttempt();
  } else if (cmd.type == TransportCmdType::DISCONNECT) {
    if (cmd.epoch < current_epoch_) {
      return;
    }
    Serial.printf("[NET:DISCONNECT] epoch=%lu\n", static_cast<unsigned long>(current_epoch_));
    desired_connected_ = false;
    is_connected_ = false;
    connection_was_open_ = false;
    state_ = TransportState::DOWN;
    next_retry_ms_ = 0;
    ws_.disconnect();
    TlsArbiter::instance().releaseGemini();
  } else if (cmd.type == TransportCmdType::SHUTDOWN) {
    desired_connected_ = false;
    is_connected_ = false;
    connection_was_open_ = false;
    state_ = TransportState::DOWN;
    ws_.disconnect();
    running_ = false;
    TlsArbiter::instance().releaseGemini();
  }
}

void GeminiTransport::drainOutbound() {
  if (!is_connected_) return;

  for (int i = 0; i < 4; ++i) {
    size_t item_size = 0;
    void* item = xRingbufferReceive(outbound_ring_, &item_size, 0);
    if (!item) break;

    OutboundHeader* hdr = static_cast<OutboundHeader*>(item);
    if (hdr->epoch == current_epoch_ && is_connected_) {
      const char* payload = reinterpret_cast<const char*>(item) + sizeof(OutboundHeader);
      ws_.sendTXT(payload, hdr->length);
    }

    if (hdr->kind == OutboundKind::AUDIO) {
      size_t consumed = sizeof(OutboundHeader) + hdr->length;
      if (outbound_audio_bytes_queued_ >= consumed) {
        outbound_audio_bytes_queued_ -= consumed;
      } else {
        outbound_audio_bytes_queued_ = 0;
      }
    }

    vRingbufferReturnItem(outbound_ring_, item);
  }
}

void GeminiTransport::wsEventStatic(WStype_t type, uint8_t* payload, size_t length) {
  if (instance_) {
    instance_->handleWsEvent(type, payload, length);
  }
}

void GeminiTransport::handleWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  InboundEventType evt_type;
  switch (type) {
    case WStype_CONNECTED:
      state_ = TransportState::OPEN;
      connection_was_open_ = true;
      is_connected_ = true;
      retry_count_ = 0;
      evt_type = InboundEventType::CONNECTED;
      Serial.printf("[NET:CONNECTED] epoch=%lu\n", static_cast<unsigned long>(current_epoch_));
      break;

    case WStype_DISCONNECTED: {
      is_connected_ = false;

      // NET-2.1 Rule 4: During ws_.disconnect() pre-retry cleanup, suppress completely
      if (in_tls_cleanup_) {
        return; // INTERNAL_PREOPEN_CLEANUP: do not notify Core 1, do not alter retry timer/count
      }

      // Rules 4, 7, 8: Distinguish between failure before OPEN and drop after OPEN
      if (state_ == TransportState::OPEN && connection_was_open_) {
        // Real session drop after OPEN! Forward to Core 1 ONCE
        state_ = TransportState::DOWN;
        desired_connected_ = false;
        connection_was_open_ = false;
        evt_type = InboundEventType::DISCONNECTED;
        Serial.printf("[NET:DISCONNECTED] epoch=%lu (was OPEN -> forwarding to Core1)\n",
                      static_cast<unsigned long>(current_epoch_));
        TlsArbiter::instance().releaseGemini();
        // Fall through to forward to Core 1
      } else {
        // Pre-open failure or disconnect while DOWN/CONNECTING
        if (!desired_connected_) {
          // Rule 8: Intentional disconnect or already DOWN -> suppress event to Core 1
          state_ = TransportState::DOWN;
          TlsArbiter::instance().releaseGemini();
          return;
        }

        // Rule 4: Pre-open failure while desired_connected == true
        retry_count_++;
        state_ = TransportState::BACKOFF;
        next_retry_ms_ = millis() + RETRY_BACKOFF_MS;
        Serial.printf("[NET:RETRY] epoch=%lu retry=%u backoff_ms=5000 reason=pre_open_failure\n",
                      static_cast<unsigned long>(current_epoch_),
                      static_cast<unsigned>(retry_count_));
        return; // Suppress event to Core 1
      }
      break;
    }

    case WStype_TEXT:
      if (state_ != TransportState::OPEN) return;
      evt_type = InboundEventType::TEXT_FRAME;
      break;

    case WStype_BIN:
      if (state_ != TransportState::OPEN) return;
      evt_type = InboundEventType::BINARY_FRAME;
      break;

    case WStype_ERROR:
      evt_type = InboundEventType::ERROR_FRAME;
      break;

    case WStype_PONG:
      evt_type = InboundEventType::PONG_FRAME;
      break;

    default:
      return;
  }

  size_t total_size = sizeof(InboundEventHeader) + length;
  void* item = nullptr;
  if (xRingbufferSendAcquire(inbound_ring_, &item, total_size, 0) == pdTRUE) {
    InboundEventHeader* hdr = static_cast<InboundEventHeader*>(item);
    hdr->type = evt_type;
    hdr->epoch = current_epoch_;
    hdr->length = length;
    if (length > 0 && payload) {
      memcpy(static_cast<uint8_t*>(item) + sizeof(InboundEventHeader), payload, length);
    }
    xRingbufferSendComplete(inbound_ring_, item);

    size_t cur_free = xRingbufferGetCurFreeSize(inbound_ring_);
    size_t used = INBOUND_BUFFER_BYTES > cur_free ? (INBOUND_BUFFER_BYTES - cur_free) : 0;
    if (used > rx_hwm_) rx_hwm_ = used;
  } else {
    rx_dropped_++;
    Serial.printf("[NET:RING] rx_drop len=%u epoch=%lu\n",
                  static_cast<unsigned>(length), static_cast<unsigned long>(current_epoch_));
  }
}

